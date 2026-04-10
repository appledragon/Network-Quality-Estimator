// NetworkQualityStore.h - Persistent storage for network quality cache
// Inspired by Chromium's net/nqe/network_quality_store.h
#pragma once

#include "nqe/NetworkID.h"
#include "nqe/CachedNetworkQuality.h"
#include <map>
#include <string>
#include <mutex>
#include <optional>
#include <chrono>

namespace nqe {

/**
 * NetworkQualityStore manages persistent storage of network quality estimates.
 * 
 * This follows Chromium's approach of:
 * - Storing cached quality per NetworkID
 * - Persisting data to disk for cross-session caching
 * - Providing quick lookup for known networks
 * - Automatic cache expiration
 * 
 * The store maintains an in-memory cache backed by disk storage,
 * allowing network quality estimates to persist across application restarts.
 */
class NetworkQualityStore {
public:
  /**
   * Configuration options for the store
   */
  struct Options {
    /// Maximum number of networks to cache (oldest evicted when exceeded)
    size_t max_cache_size;
    
    /// Maximum age for cached data (older entries are discarded)
    std::chrono::hours max_cache_age;
    
    /// Path to persistent storage file (empty = no persistence)
    std::string storage_file_path;
    
    /// Auto-save interval (0 = manual save only)
    std::chrono::seconds auto_save_interval;

    Options() 
      : max_cache_size(100),
        max_cache_age(24 * 7),  // 7 days
        storage_file_path(),
        auto_save_interval(0) {}
  };

  /**
   * Constructor
   * @param opts Configuration options
   */
  explicit NetworkQualityStore(const Options& opts);

  /**
   * Destructor - saves data if persistence is enabled
   */
  ~NetworkQualityStore();

  /**
   * Get cached quality for a network
   * @param network_id Network identifier
   * @return Cached quality if available and fresh, nullopt otherwise
   */
  std::optional<CachedNetworkQuality> get(const NetworkID& network_id) const;

  /**
   * Store quality estimate for a network
   * @param network_id Network identifier
   * @param quality Quality metrics to cache
   */
  void put(const NetworkID& network_id, const CachedNetworkQuality& quality);

  /**
   * Remove a network from cache
   * @param network_id Network identifier
   */
  void remove(const NetworkID& network_id);

  /**
   * Clear all cached data
   */
  void clear();

  /**
   * Get number of cached networks
   */
  size_t size() const;

  /**
   * Check if a network is in cache
   */
  bool contains(const NetworkID& network_id) const;

  /**
   * Load cached data from disk
   * @return true if loaded successfully, false otherwise
   */
  bool load();

  /**
   * Save cached data to disk
   * @return true if saved successfully, false otherwise
   */
  bool save() const;

  /**
   * Get all cached networks (for debugging/testing)
   */
  std::map<NetworkID, CachedNetworkQuality> getAll() const;

  /**
   * Prune stale entries based on max_cache_age
   * @return Number of entries removed
   */
  size_t pruneStaleEntries();

private:
  mutable std::mutex mutex_;
  Options options_;
  std::map<NetworkID, CachedNetworkQuality> cache_;
  
  // Helper methods
  void evictOldestIfNeeded();
  bool loadFromFile(const std::string& filepath);
  bool saveToFile(const std::string& filepath) const;
};

} // namespace nqe
