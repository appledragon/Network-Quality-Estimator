// NetworkQualityStore.cpp - Implementation of persistent network quality storage
#include "nqe/NetworkQualityStore.h"
#include "nqe/Logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace nqe {

NetworkQualityStore::NetworkQualityStore(const Options& opts)
  : options_(opts) {
  
  // Load from disk if persistence is enabled
  if (!options_.storage_file_path.empty()) {
    load();
  }
}

NetworkQualityStore::~NetworkQualityStore() {
  // Auto-save on destruction if persistence is enabled
  if (!options_.storage_file_path.empty()) {
    save();
  }
}

std::optional<CachedNetworkQuality> NetworkQualityStore::get(const NetworkID& network_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto it = cache_.find(network_id);
  if (it == cache_.end()) {
    return std::nullopt;
  }
  
  // Check if cached data is still fresh
  if (!it->second.isFresh(options_.max_cache_age)) {
    Logger::instance().debug("Cached data for " + network_id.toString() + " is stale");
    return std::nullopt;
  }
  
  Logger::instance().debug("Retrieved cached quality for " + network_id.toString() + ": " + it->second.toString());
  return it->second;
}

void NetworkQualityStore::put(const NetworkID& network_id, const CachedNetworkQuality& quality) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  if (!network_id.isValid()) {
    Logger::instance().warning("Attempted to cache quality for invalid NetworkID");
    return;
  }
  
  cache_[network_id] = quality;
  Logger::instance().debug("Cached quality for " + network_id.toString() + ": " + quality.toString());
  
  // Evict oldest entry if cache is too large
  evictOldestIfNeeded();
}

void NetworkQualityStore::remove(const NetworkID& network_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.erase(network_id);
  Logger::instance().debug("Removed cached quality for " + network_id.toString());
}

void NetworkQualityStore::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.clear();
  Logger::instance().info("Cleared all cached network quality data");
}

size_t NetworkQualityStore::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cache_.size();
}

bool NetworkQualityStore::contains(const NetworkID& network_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cache_.find(network_id) != cache_.end();
}

std::map<NetworkID, CachedNetworkQuality> NetworkQualityStore::getAll() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cache_;
}

size_t NetworkQualityStore::pruneStaleEntries() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  size_t count_before = cache_.size();
  
  // Remove entries that are too old
  auto it = cache_.begin();
  while (it != cache_.end()) {
    if (!it->second.isFresh(options_.max_cache_age)) {
      Logger::instance().debug("Pruning stale entry for " + it->first.toString());
      it = cache_.erase(it);
    } else {
      ++it;
    }
  }
  
  size_t removed = count_before - cache_.size();
  if (removed > 0) {
    Logger::instance().info("Pruned " + std::to_string(removed) + " stale cache entries");
  }
  
  return removed;
}

void NetworkQualityStore::evictOldestIfNeeded() {
  // Must be called with mutex locked
  
  if (cache_.size() <= options_.max_cache_size) {
    return;
  }
  
  // Find oldest entry by last_update_time
  auto oldest_it = cache_.begin();
  for (auto it = cache_.begin(); it != cache_.end(); ++it) {
    if (it->second.last_update_time() < oldest_it->second.last_update_time()) {
      oldest_it = it;
    }
  }
  
  Logger::instance().debug("Evicting oldest entry: " + oldest_it->first.toString());
  cache_.erase(oldest_it);
}

bool NetworkQualityStore::load() {
  if (options_.storage_file_path.empty()) {
    return false;
  }
  
  return loadFromFile(options_.storage_file_path);
}

bool NetworkQualityStore::save() const {
  if (options_.storage_file_path.empty()) {
    return false;
  }
  
  return saveToFile(options_.storage_file_path);
}

bool NetworkQualityStore::loadFromFile(const std::string& filepath) {
  std::ifstream file(filepath);
  if (!file.is_open()) {
    Logger::instance().debug("No existing cache file at " + filepath);
    return false;
  }
  
  std::lock_guard<std::mutex> lock(mutex_);
  
  // Preserve existing valid entries; only add from file
  std::string line;
  int loaded_count = 0;
  int line_num = 0;
  
  // Simple CSV format: type,name,signal,http_rtt,transport_rtt,throughput,ect,age_seconds
  while (std::getline(file, line)) {
    line_num++;
    
    if (line.empty() || line[0] == '#') {
      continue;  // Skip empty lines and comments
    }
    
    std::istringstream iss(line);
    std::string token;
    std::vector<std::string> fields;
    
    while (std::getline(iss, token, ',')) {
      fields.push_back(token);
    }
    
    if (fields.size() != 8) {
      Logger::instance().warning("Invalid cache file format at line " + std::to_string(line_num));
      continue;
    }
    
    try {
      // Parse and validate ConnectionType enum range
      int type_val = std::stoi(fields[0]);
      if (type_val < 0 || type_val > static_cast<int>(NetworkID::ConnectionType::NONE)) {
        Logger::instance().warning("Invalid connection type at line " + std::to_string(line_num));
        continue;
      }
      auto type = static_cast<NetworkID::ConnectionType>(type_val);
      std::string name = fields[1];
      int signal = std::stoi(fields[2]);
      NetworkID network_id(type, name, signal);
      
      // Parse quality metrics
      auto parse_optional = [](const std::string& s) -> std::optional<double> {
        if (s.empty() || s == "-") return std::nullopt;
        return std::stod(s);
      };
      
      auto http_rtt = parse_optional(fields[3]);
      auto transport_rtt = parse_optional(fields[4]);
      auto throughput = parse_optional(fields[5]);
      
      // Validate ECT enum range
      int ect_val = std::stoi(fields[6]);
      if (ect_val < 0 || ect_val > static_cast<int>(EffectiveConnectionType::LAST)) {
        Logger::instance().warning("Invalid ECT value at line " + std::to_string(line_num));
        continue;
      }
      auto ect = static_cast<EffectiveConnectionType>(ect_val);
      
      // Parse age in seconds and calculate timestamp relative to now
      long long age_seconds = std::stoll(fields[7]);
      if (age_seconds < 0) {
        Logger::instance().warning("Invalid age at line " + std::to_string(line_num));
        continue;
      }
      auto now = std::chrono::steady_clock::now();
      auto timestamp = now - std::chrono::seconds(age_seconds);
      
      CachedNetworkQuality quality(http_rtt, transport_rtt, throughput, ect, timestamp);
      
      // Only load if fresh
      if (quality.isFresh(options_.max_cache_age)) {
        cache_[network_id] = quality;
        loaded_count++;
      }
    } catch (const std::exception& e) {
      Logger::instance().warning("Error parsing cache line " + std::to_string(line_num) + ": " + e.what());
      continue;  // Skip this line, preserve other loaded entries
    }
  }
  
  Logger::instance().info("Loaded " + std::to_string(loaded_count) + " cached network quality entries from " + filepath);
  return loaded_count > 0;
}

bool NetworkQualityStore::saveToFile(const std::string& filepath) const {
  std::lock_guard<std::mutex> lock(mutex_);
  
  std::ofstream file(filepath);
  if (!file.is_open()) {
    Logger::instance().error("Failed to open cache file for writing: " + filepath);
    return false;
  }
  
  try {
    // Write header
    file << "# NQE Network Quality Cache\n";
    file << "# Format: type,name,signal,http_rtt,transport_rtt,throughput,ect,age_seconds\n";
    
    auto now = std::chrono::steady_clock::now();
    
    // Write each entry
    for (const auto& [network_id, quality] : cache_) {
      // Skip stale entries
      if (!quality.isFresh(options_.max_cache_age)) {
        continue;
      }
      
      // Sanitize network name: replace commas to avoid breaking CSV format
      std::string safe_name = network_id.name();
      std::replace(safe_name.begin(), safe_name.end(), ',', '_');
      
      file << static_cast<int>(network_id.type()) << ","
           << safe_name << ","
           << network_id.signal_strength() << ",";
      
      // Write optional metrics (use "-" for missing values)
      auto write_optional = [&file](const std::optional<double>& val) {
        if (val) {
          file << *val;
        } else {
          file << "-";
        }
      };
      
      write_optional(quality.http_rtt_ms());
      file << ",";
      write_optional(quality.transport_rtt_ms());
      file << ",";
      write_optional(quality.downstream_throughput_kbps());
      file << ",";
      
      file << static_cast<int>(quality.effective_type()) << ",";
      
      // Write age in seconds (so we can restore relative to load time)
      auto age_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now - quality.last_update_time()).count();
      file << age_seconds << "\n";
    }
    
    Logger::instance().info("Saved " + std::to_string(cache_.size()) + " cached network quality entries to " + filepath);
    return true;
    
  } catch (const std::exception& e) {
    Logger::instance().error("Error saving cache file: " + std::string(e.what()));
    return false;
  }
}

} // namespace nqe
