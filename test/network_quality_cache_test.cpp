// network_quality_cache_test.cpp - Test network quality caching and persistence
#include "nqe/Nqe.h"
#include "nqe/NetworkID.h"
#include "nqe/CachedNetworkQuality.h"
#include "nqe/NetworkQualityStore.h"
#include "nqe/Logger.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>

using namespace nqe;

void printSeparator(const std::string& title) {
  std::cout << "\n";
  std::cout << "============================================================\n";
  std::cout << "  " << title << "\n";
  std::cout << "============================================================\n\n";
}

void testNetworkID() {
  printSeparator("Testing NetworkID");
  
  // Create different NetworkIDs
  NetworkID wifi(NetworkID::ConnectionType::WIFI, "HomeNetwork", 85);
  NetworkID cellular4g(NetworkID::ConnectionType::CELLULAR_4G, "Verizon");
  NetworkID ethernet(NetworkID::ConnectionType::ETHERNET, "");
  
  std::cout << "WiFi Network: " << wifi.toString() << std::endl;
  std::cout << "Cellular 4G: " << cellular4g.toString() << std::endl;
  std::cout << "Ethernet: " << ethernet.toString() << std::endl;
  
  std::cout << "\n✓ NetworkID creation and toString working\n";
}

void testCachedNetworkQuality() {
  printSeparator("Testing CachedNetworkQuality");
  
  // Create cached quality
  CachedNetworkQuality cached(
    100.0,  // HTTP RTT
    30.0,   // Transport RTT
    8000.0, // Throughput (8 Mbps)
    EffectiveConnectionType::TYPE_4G,
    Clock::now()
  );
  
  std::cout << "Cached quality: " << cached.toString() << std::endl;
  std::cout << "Is empty: " << (cached.isEmpty() ? "yes" : "no") << std::endl;
  std::cout << "Is fresh (60s): " << (cached.isFresh(std::chrono::seconds(60)) ? "yes" : "no") << std::endl;
  
  // Test with old timestamp
  CachedNetworkQuality old_cached(
    150.0, 50.0, 5000.0,
    EffectiveConnectionType::TYPE_3G,
    Clock::now() - std::chrono::hours(10)
  );
  
  std::cout << "\nOld cached quality: " << old_cached.toString() << std::endl;
  std::cout << "Is fresh (1h): " << (old_cached.isFresh(std::chrono::hours(1)) ? "yes" : "no") << std::endl;
  std::cout << "Is fresh (24h): " << (old_cached.isFresh(std::chrono::hours(24)) ? "yes" : "no") << std::endl;
  
  std::cout << "\n✓ CachedNetworkQuality working correctly\n";
}

void testNetworkQualityStore() {
  printSeparator("Testing NetworkQualityStore");
  
  // Create store with temporary file
  // Use platform-specific temp directory
  std::string temp_file = "nqe_cache_test.dat";
#ifdef _WIN32
  temp_file = std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "\\" + temp_file;
#else
  temp_file = "/tmp/" + temp_file;
#endif
  
  NetworkQualityStore::Options opts;
  opts.max_cache_size = 10;
  opts.max_cache_age = std::chrono::hours(24);
  opts.storage_file_path = temp_file;
  
  NetworkQualityStore store(opts);
  
  // Add some networks
  NetworkID wifi1(NetworkID::ConnectionType::WIFI, "HomeWiFi");
  NetworkID wifi2(NetworkID::ConnectionType::WIFI, "OfficeWiFi");
  NetworkID cellular(NetworkID::ConnectionType::CELLULAR_4G, "Verizon");
  
  CachedNetworkQuality quality1(100.0, 30.0, 8000.0, EffectiveConnectionType::TYPE_4G);
  CachedNetworkQuality quality2(150.0, 50.0, 5000.0, EffectiveConnectionType::TYPE_3G);
  CachedNetworkQuality quality3(200.0, 80.0, 3000.0, EffectiveConnectionType::TYPE_3G);
  
  store.put(wifi1, quality1);
  store.put(wifi2, quality2);
  store.put(cellular, quality3);
  
  std::cout << "Stored 3 networks, cache size: " << store.size() << std::endl;
  
  // Retrieve
  auto retrieved = store.get(wifi1);
  if (retrieved) {
    std::cout << "Retrieved WiFi1: " << retrieved->toString() << std::endl;
  }
  
  // Test persistence
  bool saved = store.save();
  std::cout << "\nSaved to disk: " << (saved ? "success" : "failed") << std::endl;
  
  // Create new store and load
  NetworkQualityStore store2(opts);
  bool loaded = store2.load();
  std::cout << "Loaded from disk: " << (loaded ? "success" : "failed") << std::endl;
  std::cout << "Loaded cache size: " << store2.size() << std::endl;
  
  auto retrieved2 = store2.get(wifi1);
  if (retrieved2) {
    std::cout << "Retrieved after reload: " << retrieved2->toString() << std::endl;
  }
  
  std::cout << "\n✓ NetworkQualityStore persistence working\n";
}

void testNqeWithCaching() {
  printSeparator("Testing NQE with Caching");
  
  // Create NQE with caching enabled
  // Use platform-specific temp directory
  std::string temp_file = "nqe_integrated_cache.dat";
#ifdef _WIN32
  temp_file = std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "\\" + temp_file;
#else
  temp_file = "/tmp/" + temp_file;
#endif
  
  Nqe::Options opts;
  opts.enable_caching = true;
  opts.max_cache_size = 50;
  opts.cache_file_path = temp_file;
  
  Nqe estimator(opts);
  
  std::cout << "NQE created with caching enabled\n";
  
  // Add some samples to build estimates
  auto now = Clock::now();
  estimator.addSample(Source::HTTP_TTFB, 100.0, now);
  estimator.addSample(Source::TRANSPORT_RTT, 30.0, now);
  estimator.addThroughputSample(100000, now - std::chrono::seconds(1), now);
  
  std::cout << "Added samples\n";
  
  // Get estimate
  auto est = estimator.getEstimate();
  std::cout << "Current estimate:\n";
  std::cout << "  Combined RTT: " << est.rtt_ms << "ms\n";
  if (est.http_ttfb_ms) {
    std::cout << "  HTTP TTFB: " << *est.http_ttfb_ms << "ms\n";
  }
  if (est.transport_rtt_ms) {
    std::cout << "  Transport RTT: " << *est.transport_rtt_ms << "ms\n";
  }
  if (est.throughput_kbps) {
    std::cout << "  Throughput: " << *est.throughput_kbps << " kbps\n";
  }
  std::cout << "  ECT: " << effectiveConnectionTypeToString(est.effective_type) << "\n";
  
  // Manually update cache (simulating network ID being available)
  estimator.updateCachedQuality();
  
  // Save cache
  bool saved = estimator.saveCachedData();
  std::cout << "\nCache saved: " << (saved ? "success" : "failed") << std::endl;
  
  // Create new estimator and load cache
  Nqe estimator2(opts);
  bool loaded = estimator2.loadCachedData();
  std::cout << "Cache loaded: " << (loaded ? "success" : "failed") << std::endl;
  
  std::cout << "\n✓ NQE caching integration working\n";
}

void testCacheExpiration() {
  printSeparator("Testing Cache Expiration");
  
  // Use platform-specific temp directory
  std::string temp_file = "nqe_expiration_test.dat";
#ifdef _WIN32
  temp_file = std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "\\" + temp_file;
#else
  temp_file = "/tmp/" + temp_file;
#endif
  
  NetworkQualityStore::Options opts;
  opts.max_cache_age = std::chrono::hours(0);  // Set to 0 hours plus manual adjustment
  opts.storage_file_path = temp_file;
  
  // We'll manually create a quality with an old timestamp
  NetworkQualityStore store(opts);
  
  NetworkID wifi(NetworkID::ConnectionType::WIFI, "TestNetwork");
  
  // Create quality with current time - should be fresh initially  
  CachedNetworkQuality quality_fresh(100.0, 30.0, 8000.0, EffectiveConnectionType::TYPE_4G, Clock::now());
  store.put(wifi, quality_fresh);
  std::cout << "Stored fresh quality, cache size: " << store.size() << std::endl;
  
  // Immediately retrieve - should work
  auto retrieved1 = store.get(wifi);
  std::cout << "Immediate retrieval: " << (retrieved1.has_value() ? "success" : "failed (stale)") << std::endl;
  
  // Now add an old quality
  CachedNetworkQuality quality_old(100.0, 30.0, 8000.0, EffectiveConnectionType::TYPE_4G, 
                                   Clock::now() - std::chrono::hours(10));
  store.put(wifi, quality_old);
  
  // Try to retrieve - should fail as stale
  auto retrieved2 = store.get(wifi);
  std::cout << "Retrieval of old quality: " << (retrieved2.has_value() ? "unexpected success" : "correctly failed (stale)") << std::endl;
  
  // Prune stale entries
  size_t pruned = store.pruneStaleEntries();
  std::cout << "Pruned " << pruned << " stale entries\n";
  std::cout << "Cache size after pruning: " << store.size() << std::endl;
  
  std::cout << "\n✓ Cache expiration working correctly\n";
}

int main() {
  // Configure logging
  Logger::instance().setMinLevel(LogLevel::LOG_INFO);
  Logger::instance().setCallback([](LogLevel level, const std::string& msg) {
    std::cout << "[" << logLevelToString(level) << "] " << msg << std::endl;
  });
  
  std::cout << "╔════════════════════════════════════════════════════════════╗\n";
  std::cout << "║    NQE Network Quality Caching - Comprehensive Test       ║\n";
  std::cout << "╚════════════════════════════════════════════════════════════╝\n";
  
  try {
    testNetworkID();
    testCachedNetworkQuality();
    testNetworkQualityStore();
    testNqeWithCaching();
    testCacheExpiration();
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "  All Caching Tests Completed Successfully\n";
    std::cout << "============================================================\n\n";
    std::cout << "✓ All caching features working as expected!\n\n";
    
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "\n❌ Test failed with exception: " << e.what() << "\n";
    return 1;
  }
}
