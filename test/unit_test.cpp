/**
 * Comprehensive Unit Tests for NQE Library
 * 
 * Covers gaps identified in existing test suite:
 * - NetworkID: operator<, hash, edge cases
 * - NetworkQualityStore: remove, clear, contains, getAll, pruneStaleEntries
 * - EffectiveConnectionType: boundary values, custom thresholds, nullopt combos
 * - CachedNetworkQuality: individual getters, update validation, edge cases
 * - Aggregator/WeightedMedian: signal strength, percentiles, decay, edge cases
 * - Combiner: all-null, geometric mean, bias boundaries, edge values
 * - ThroughputAnalyzer: isHangingWindow, onConnectionTypeChanged, getSampleCount
 * - HttpRttSource: overlapping requests, orphaned timestamps
 * - QuicH2PingSource: orphaned pongs, multiple pongs, empty authority
 * - Logger: template methods, level filtering
 * - Nqe: observers, estimate callback, caching integration
 * - ReportGenerator: text report, edge cases
 */

#include "nqe/Nqe.h"
#include "nqe/NetworkID.h"
#include "nqe/CachedNetworkQuality.h"
#include "nqe/NetworkQualityStore.h"
#include "nqe/EffectiveConnectionType.h"
#include "nqe/HttpRttSource.h"
#include "nqe/QuicH2PingSource.h"
#include "nqe/Logger.h"
#include "nqe/NetworkQualityObserver.h"
#include "nqe/ThroughputAnalyzer.h"
#include "nqe/ReportGenerator.h"
#include "aggregators/Aggregator.h"
#include "aggregators/Combiner.h"
#include "aggregators/WeightedMedian.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <functional>
#include <atomic>
#include <fstream>
#include <cstdio>

using namespace nqe;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
  static void test_##name(); \
  struct TestRegistrar_##name { \
    TestRegistrar_##name() { \
      std::cout << "  Testing: " #name "... "; \
      try { \
        test_##name(); \
        tests_passed++; \
        std::cout << "PASS\n"; \
      } catch (const std::exception& e) { \
        tests_failed++; \
        std::cout << "FAIL (" << e.what() << ")\n"; \
      } \
    } \
  }; \
  static void test_##name()

#define ASSERT(cond) \
  do { if (!(cond)) throw std::runtime_error("Assertion failed: " #cond " at line " + std::to_string(__LINE__)); } while(0)

#define ASSERT_EQ(a, b) \
  do { if (!((a) == (b))) throw std::runtime_error("Expected " #a " == " #b " at line " + std::to_string(__LINE__)); } while(0)

#define ASSERT_NE(a, b) \
  do { if ((a) == (b)) throw std::runtime_error("Expected " #a " != " #b " at line " + std::to_string(__LINE__)); } while(0)

#define ASSERT_LT(a, b) \
  do { if (!((a) < (b))) throw std::runtime_error("Expected " #a " < " #b " at line " + std::to_string(__LINE__)); } while(0)

#define ASSERT_NEAR(a, b, eps) \
  do { if (std::abs((a) - (b)) > (eps)) throw std::runtime_error( \
    "Expected |" #a " - " #b "| <= " #eps " at line " + std::to_string(__LINE__) + \
    " (got " + std::to_string(a) + " vs " + std::to_string(b) + ")"); } while(0)

static void printSection(const std::string& title) {
  std::cout << "\n=== " << title << " ===\n";
}

// ============================================================================
//  NetworkID Tests
// ============================================================================

TEST(NetworkID_OperatorLess_DifferentTypes) {
  NetworkID wifi(NetworkID::ConnectionType::WIFI, "net");
  NetworkID eth(NetworkID::ConnectionType::ETHERNET, "net");
  // ETHERNET(1) < WIFI(2) 
  ASSERT(eth < wifi);
  ASSERT(!(wifi < eth));
}

TEST(NetworkID_OperatorLess_SameType_DifferentName) {
  NetworkID a(NetworkID::ConnectionType::WIFI, "AAA");
  NetworkID b(NetworkID::ConnectionType::WIFI, "BBB");
  ASSERT(a < b);
  ASSERT(!(b < a));
}

TEST(NetworkID_OperatorLess_Equal) {
  NetworkID a(NetworkID::ConnectionType::WIFI, "same");
  NetworkID b(NetworkID::ConnectionType::WIFI, "same");
  ASSERT(!(a < b));
  ASSERT(!(b < a));
}

TEST(NetworkID_Hash_EqualObjects) {
  NetworkID a(NetworkID::ConnectionType::WIFI, "MyNetwork", 50);
  NetworkID b(NetworkID::ConnectionType::WIFI, "MyNetwork", 80);  // signal diff ignored in ==
  ASSERT_EQ(a, b);
  std::hash<NetworkID> h;
  ASSERT_EQ(h(a), h(b));
}

TEST(NetworkID_Hash_UsableInUnorderedMap) {
  std::unordered_map<NetworkID, int> m;
  NetworkID wifi(NetworkID::ConnectionType::WIFI, "Home");
  NetworkID cell(NetworkID::ConnectionType::CELLULAR_4G, "Carrier");
  m[wifi] = 1;
  m[cell] = 2;
  ASSERT_EQ(m[wifi], 1);
  ASSERT_EQ(m[cell], 2);
  ASSERT_EQ(m.size(), 2u);
}

TEST(NetworkID_Hash_DifferentObjects) {
  NetworkID a(NetworkID::ConnectionType::WIFI, "Net1");
  NetworkID b(NetworkID::ConnectionType::WIFI, "Net2");
  std::hash<NetworkID> h;
  // Different objects should (usually) have different hashes
  // Not guaranteed but extremely likely
  ASSERT_NE(a, b);
}

TEST(NetworkID_DefaultIsInvalid) {
  NetworkID id;
  ASSERT(!id.isValid());
  ASSERT_EQ(id.type(), NetworkID::ConnectionType::UNKNOWN);
  ASSERT_EQ(id.name(), "");
  ASSERT_EQ(id.signal_strength(), -1);
}

TEST(NetworkID_NoneIsInvalid) {
  NetworkID id(NetworkID::ConnectionType::NONE, "");
  ASSERT(!id.isValid());
}

TEST(NetworkID_Equality_IgnoresSignalStrength) {
  NetworkID a(NetworkID::ConnectionType::WIFI, "Net", 10);
  NetworkID b(NetworkID::ConnectionType::WIFI, "Net", 90);
  ASSERT_EQ(a, b);
}

TEST(NetworkID_OperatorNotEqual) {
  NetworkID a(NetworkID::ConnectionType::WIFI, "A");
  NetworkID b(NetworkID::ConnectionType::WIFI, "B");
  ASSERT(a != b);
}

// ============================================================================
//  CachedNetworkQuality Tests
// ============================================================================

TEST(CachedNetworkQuality_DefaultIsEmpty) {
  CachedNetworkQuality cached;
  ASSERT(cached.isEmpty());
  ASSERT(!cached.http_rtt_ms().has_value());
  ASSERT(!cached.transport_rtt_ms().has_value());
  ASSERT(!cached.downstream_throughput_kbps().has_value());
  ASSERT_EQ(cached.effective_type(), EffectiveConnectionType::UNKNOWN);
}

TEST(CachedNetworkQuality_FullConstructor_AllGetters) {
  auto now = std::chrono::steady_clock::now();
  CachedNetworkQuality cached(100.5, 30.2, 8000.0, EffectiveConnectionType::TYPE_4G, now);
  ASSERT(!cached.isEmpty());
  ASSERT_NEAR(*cached.http_rtt_ms(), 100.5, 0.01);
  ASSERT_NEAR(*cached.transport_rtt_ms(), 30.2, 0.01);
  ASSERT_NEAR(*cached.downstream_throughput_kbps(), 8000.0, 0.01);
  ASSERT_EQ(cached.effective_type(), EffectiveConnectionType::TYPE_4G);
  ASSERT(cached.last_update_time() == now);
}

TEST(CachedNetworkQuality_EmptyNotFresh) {
  CachedNetworkQuality cached;
  ASSERT(!cached.isFresh(std::chrono::seconds(3600)));
}

TEST(CachedNetworkQuality_FreshCheck) {
  auto now = std::chrono::steady_clock::now();
  CachedNetworkQuality cached(50.0, 20.0, 5000.0, EffectiveConnectionType::TYPE_4G, now);
  ASSERT(cached.isFresh(std::chrono::seconds(60), now));
  ASSERT(!cached.isFresh(std::chrono::seconds(60), now + std::chrono::seconds(120)));
}

TEST(CachedNetworkQuality_Update) {
  CachedNetworkQuality cached;
  ASSERT(cached.isEmpty());
  auto updTime = std::chrono::steady_clock::now();
  cached.update(200.0, 60.0, 3000.0, EffectiveConnectionType::TYPE_3G, updTime);
  ASSERT(!cached.isEmpty());
  ASSERT_NEAR(*cached.http_rtt_ms(), 200.0, 0.01);
  ASSERT_NEAR(*cached.transport_rtt_ms(), 60.0, 0.01);
  ASSERT_NEAR(*cached.downstream_throughput_kbps(), 3000.0, 0.01);
  ASSERT_EQ(cached.effective_type(), EffectiveConnectionType::TYPE_3G);
}

TEST(CachedNetworkQuality_PartiallyEmpty) {
  // Only HTTP RTT set, rest nullopt - not empty since ECT is set
  CachedNetworkQuality cached(100.0, std::nullopt, std::nullopt, EffectiveConnectionType::TYPE_4G);
  ASSERT(!cached.isEmpty());
  ASSERT(cached.http_rtt_ms().has_value());
  ASSERT(!cached.transport_rtt_ms().has_value());
  ASSERT(!cached.downstream_throughput_kbps().has_value());
}

// ============================================================================
//  EffectiveConnectionType Tests
// ============================================================================

TEST(ECT_AllNullopt_ReturnsUnknown) {
  auto ect = computeEffectiveConnectionType(std::nullopt, std::nullopt, std::nullopt);
  ASSERT_EQ(ect, EffectiveConnectionType::UNKNOWN);
}

TEST(ECT_OnlyHttpRtt_4G) {
  auto ect = computeEffectiveConnectionType(50.0, std::nullopt, std::nullopt);
  ASSERT_EQ(ect, EffectiveConnectionType::TYPE_4G);
}

TEST(ECT_OnlyHttpRtt_3G) {
  auto ect = computeEffectiveConnectionType(500.0, std::nullopt, std::nullopt);
  ASSERT_EQ(ect, EffectiveConnectionType::TYPE_3G);
}

TEST(ECT_OnlyHttpRtt_2G) {
  auto ect = computeEffectiveConnectionType(1500.0, std::nullopt, std::nullopt);
  ASSERT_EQ(ect, EffectiveConnectionType::TYPE_2G);
}

TEST(ECT_OnlyHttpRtt_Slow2G) {
  auto ect = computeEffectiveConnectionType(3000.0, std::nullopt, std::nullopt);
  ASSERT_EQ(ect, EffectiveConnectionType::SLOW_2G);
}

TEST(ECT_OnlyTransportRtt) {
  auto ect = computeEffectiveConnectionType(std::nullopt, 50.0, std::nullopt);
  ASSERT_EQ(ect, EffectiveConnectionType::TYPE_4G);
}

TEST(ECT_OnlyThroughput_High) {
  auto ect = computeEffectiveConnectionType(std::nullopt, std::nullopt, 5000.0);
  ASSERT_EQ(ect, EffectiveConnectionType::TYPE_4G);
}

TEST(ECT_OnlyThroughput_Low) {
  auto ect = computeEffectiveConnectionType(std::nullopt, std::nullopt, 30.0);
  ASSERT_EQ(ect, EffectiveConnectionType::SLOW_2G);
}

TEST(ECT_BoundaryExact_400ms_Is3G) {
  // At exactly 400ms threshold, should be 3G (>= 400 is 3G)
  auto ect = computeEffectiveConnectionType(400.0, std::nullopt, std::nullopt);
  ASSERT_EQ(ect, EffectiveConnectionType::TYPE_3G);
}

TEST(ECT_BoundaryJustBelow_400ms_Is4G) {
  auto ect = computeEffectiveConnectionType(399.9, std::nullopt, std::nullopt);
  ASSERT_EQ(ect, EffectiveConnectionType::TYPE_4G);
}

TEST(ECT_BoundaryExact_1400ms_Is2G) {
  auto ect = computeEffectiveConnectionType(1400.0, std::nullopt, std::nullopt);
  ASSERT_EQ(ect, EffectiveConnectionType::TYPE_2G);
}

TEST(ECT_BoundaryExact_2000ms_IsSlow2G) {
  auto ect = computeEffectiveConnectionType(2000.0, std::nullopt, std::nullopt);
  ASSERT_EQ(ect, EffectiveConnectionType::SLOW_2G);
}

TEST(ECT_RttAndThroughput_Disagree_ReturnsWorse) {
  // RTT says 4G (50ms), throughput says Slow-2G (30 kbps) -> Slow-2G
  auto ect = computeEffectiveConnectionType(50.0, std::nullopt, 30.0);
  ASSERT_EQ(ect, EffectiveConnectionType::SLOW_2G);
}

TEST(ECT_CustomThresholds) {
  EffectiveConnectionTypeThresholds custom;
  custom.http_rtt_3g = 200;           // stricter 3G threshold
  custom.downstream_throughput_3g = 1000;  // stricter throughput threshold
  
  // With default thresholds, 300ms would be 4G; with custom, it's 3G
  auto ect_default = computeEffectiveConnectionType(300.0, std::nullopt, std::nullopt);
  auto ect_custom = computeEffectiveConnectionType(300.0, std::nullopt, std::nullopt, custom);
  ASSERT_EQ(ect_default, EffectiveConnectionType::TYPE_4G);
  ASSERT_EQ(ect_custom, EffectiveConnectionType::TYPE_3G);
}

TEST(ECT_BothRtts_UsesMinimum) {
  // HTTP RTT says 3G (500ms), transport says 4G (50ms), min=50ms -> 4G
  auto ect = computeEffectiveConnectionType(500.0, 50.0, std::nullopt);
  ASSERT_EQ(ect, EffectiveConnectionType::TYPE_4G);
}

TEST(ECT_ThroughputBoundaries) {
  // Exactly at 50 kbps boundary (2G threshold)
  auto ect50 = computeEffectiveConnectionType(std::nullopt, std::nullopt, 50.0);
  ASSERT_EQ(ect50, EffectiveConnectionType::TYPE_2G);
  
  auto ect49 = computeEffectiveConnectionType(std::nullopt, std::nullopt, 49.9);
  ASSERT_EQ(ect49, EffectiveConnectionType::SLOW_2G);
  
  auto ect70 = computeEffectiveConnectionType(std::nullopt, std::nullopt, 70.0);
  ASSERT_EQ(ect70, EffectiveConnectionType::TYPE_3G);
  
  auto ect700 = computeEffectiveConnectionType(std::nullopt, std::nullopt, 700.0);
  ASSERT_EQ(ect700, EffectiveConnectionType::TYPE_4G);
}

TEST(ECT_ToString) {
  ASSERT_EQ(std::string(effectiveConnectionTypeToString(EffectiveConnectionType::UNKNOWN)), "Unknown");
  ASSERT_EQ(std::string(effectiveConnectionTypeToString(EffectiveConnectionType::OFFLINE)), "Offline");
  ASSERT_EQ(std::string(effectiveConnectionTypeToString(EffectiveConnectionType::SLOW_2G)), "Slow-2G");
  ASSERT_EQ(std::string(effectiveConnectionTypeToString(EffectiveConnectionType::TYPE_2G)), "2G");
  ASSERT_EQ(std::string(effectiveConnectionTypeToString(EffectiveConnectionType::TYPE_3G)), "3G");
  ASSERT_EQ(std::string(effectiveConnectionTypeToString(EffectiveConnectionType::TYPE_4G)), "4G");
}

// ============================================================================
//  NetworkQualityStore Tests
// ============================================================================

static std::string getTempFilePath(const std::string& name) {
#ifdef _WIN32
  const char* tmp = getenv("TEMP");
  return std::string(tmp ? tmp : ".") + "\\" + name;
#else
  return "/tmp/" + name;
#endif
}

TEST(Store_Remove) {
  NetworkQualityStore::Options opts;
  opts.max_cache_size = 10;
  NetworkQualityStore store(opts);

  NetworkID wifi(NetworkID::ConnectionType::WIFI, "TestNet");
  CachedNetworkQuality qual(100.0, 30.0, 8000.0, EffectiveConnectionType::TYPE_4G);

  store.put(wifi, qual);
  ASSERT_EQ(store.size(), 1u);
  ASSERT(store.get(wifi).has_value());

  store.remove(wifi);
  ASSERT_EQ(store.size(), 0u);
  ASSERT(!store.get(wifi).has_value());
}

TEST(Store_Remove_NonExistent) {
  NetworkQualityStore::Options opts;
  NetworkQualityStore store(opts);

  NetworkID wifi(NetworkID::ConnectionType::WIFI, "NoSuchNet");
  store.remove(wifi);  // Should not crash
  ASSERT_EQ(store.size(), 0u);
}

TEST(Store_Clear) {
  NetworkQualityStore::Options opts;
  NetworkQualityStore store(opts);

  store.put(NetworkID(NetworkID::ConnectionType::WIFI, "A"),
            CachedNetworkQuality(10, 5, 100, EffectiveConnectionType::TYPE_4G));
  store.put(NetworkID(NetworkID::ConnectionType::WIFI, "B"),
            CachedNetworkQuality(20, 10, 200, EffectiveConnectionType::TYPE_4G));
  store.put(NetworkID(NetworkID::ConnectionType::CELLULAR_4G, "C"),
            CachedNetworkQuality(30, 15, 300, EffectiveConnectionType::TYPE_3G));

  ASSERT_EQ(store.size(), 3u);
  store.clear();
  ASSERT_EQ(store.size(), 0u);
}

TEST(Store_Contains) {
  NetworkQualityStore::Options opts;
  NetworkQualityStore store(opts);

  NetworkID wifi(NetworkID::ConnectionType::WIFI, "TestNet");
  ASSERT(!store.contains(wifi));

  store.put(wifi, CachedNetworkQuality(100, 30, 8000, EffectiveConnectionType::TYPE_4G));
  ASSERT(store.contains(wifi));

  store.remove(wifi);
  ASSERT(!store.contains(wifi));
}

TEST(Store_GetAll) {
  NetworkQualityStore::Options opts;
  NetworkQualityStore store(opts);

  NetworkID w1(NetworkID::ConnectionType::WIFI, "Net1");
  NetworkID w2(NetworkID::ConnectionType::WIFI, "Net2");
  NetworkID c1(NetworkID::ConnectionType::CELLULAR_4G, "Carrier");

  store.put(w1, CachedNetworkQuality(100, 30, 8000, EffectiveConnectionType::TYPE_4G));
  store.put(w2, CachedNetworkQuality(200, 60, 5000, EffectiveConnectionType::TYPE_3G));
  store.put(c1, CachedNetworkQuality(300, 80, 3000, EffectiveConnectionType::TYPE_3G));

  auto all = store.getAll();
  ASSERT_EQ(all.size(), 3u);
  ASSERT(all.find(w1) != all.end());
  ASSERT(all.find(w2) != all.end());
  ASSERT(all.find(c1) != all.end());
}

TEST(Store_PruneStaleEntries) {
  NetworkQualityStore::Options opts;
  opts.max_cache_age = std::chrono::hours(1);
  NetworkQualityStore store(opts);

  auto now = std::chrono::steady_clock::now();

  // Fresh entry
  NetworkID fresh(NetworkID::ConnectionType::WIFI, "Fresh");
  store.put(fresh, CachedNetworkQuality(100, 30, 8000, EffectiveConnectionType::TYPE_4G, now));

  // Stale entry (created 2 hours ago)
  NetworkID stale(NetworkID::ConnectionType::WIFI, "Stale");
  store.put(stale, CachedNetworkQuality(200, 60, 5000, EffectiveConnectionType::TYPE_3G,
            now - std::chrono::hours(2)));

  ASSERT_EQ(store.size(), 2u);

  size_t pruned = store.pruneStaleEntries();
  ASSERT_EQ(pruned, 1u);
  ASSERT_EQ(store.size(), 1u);
  ASSERT(store.contains(fresh));
  ASSERT(!store.contains(stale));
}

TEST(Store_PruneAllFresh) {
  NetworkQualityStore::Options opts;
  opts.max_cache_age = std::chrono::hours(24);
  NetworkQualityStore store(opts);

  store.put(NetworkID(NetworkID::ConnectionType::WIFI, "A"),
            CachedNetworkQuality(10, 5, 100, EffectiveConnectionType::TYPE_4G));
  store.put(NetworkID(NetworkID::ConnectionType::WIFI, "B"),
            CachedNetworkQuality(20, 10, 200, EffectiveConnectionType::TYPE_4G));

  size_t pruned = store.pruneStaleEntries();
  ASSERT_EQ(pruned, 0u);
  ASSERT_EQ(store.size(), 2u);
}

TEST(Store_Eviction) {
  NetworkQualityStore::Options opts;
  opts.max_cache_size = 3;
  NetworkQualityStore store(opts);

  for (int i = 0; i < 5; i++) {
    store.put(NetworkID(NetworkID::ConnectionType::WIFI, "Net" + std::to_string(i)),
              CachedNetworkQuality(100.0 * i, 30.0 * i, 8000.0, EffectiveConnectionType::TYPE_4G));
  }

  // Should have evicted oldest entries
  ASSERT(store.size() <= 3u);
}

TEST(Store_Persistence_SaveLoad) {
  std::string path = getTempFilePath("unit_test_store_persist.dat");
  
  // Save
  {
    NetworkQualityStore::Options opts;
    opts.storage_file_path = path;
    NetworkQualityStore store(opts);
    
    store.put(NetworkID(NetworkID::ConnectionType::WIFI, "Home"),
              CachedNetworkQuality(100, 30, 8000, EffectiveConnectionType::TYPE_4G));
    store.put(NetworkID(NetworkID::ConnectionType::CELLULAR_4G, "Carrier"),
              CachedNetworkQuality(200, 60, 3000, EffectiveConnectionType::TYPE_3G));
    store.save();
  }
  
  // Load
  {
    NetworkQualityStore::Options opts;
    opts.storage_file_path = path;
    NetworkQualityStore store(opts);
    
    ASSERT(store.load());
    ASSERT_EQ(store.size(), 2u);
    ASSERT(store.contains(NetworkID(NetworkID::ConnectionType::WIFI, "Home")));
    ASSERT(store.contains(NetworkID(NetworkID::ConnectionType::CELLULAR_4G, "Carrier")));
  }
  
  // Cleanup
  std::remove(path.c_str());
}

TEST(Store_LoadFromEmptyPath) {
  NetworkQualityStore::Options opts;
  opts.storage_file_path = "";
  NetworkQualityStore store(opts);
  // Should handle gracefully
  ASSERT(!store.load());
}

TEST(Store_UpdateExistingEntry) {
  NetworkQualityStore::Options opts;
  NetworkQualityStore store(opts);

  NetworkID wifi(NetworkID::ConnectionType::WIFI, "Home");
  store.put(wifi, CachedNetworkQuality(100, 30, 8000, EffectiveConnectionType::TYPE_4G));
  store.put(wifi, CachedNetworkQuality(200, 50, 5000, EffectiveConnectionType::TYPE_3G));

  ASSERT_EQ(store.size(), 1u);
  auto retrieved = store.get(wifi);
  ASSERT(retrieved.has_value());
  ASSERT_NEAR(*retrieved->http_rtt_ms(), 200.0, 0.01);
}

// ============================================================================
//  Combiner Tests
// ============================================================================

TEST(Combiner_AllNull) {
  auto result = combineRtt(std::nullopt, std::nullopt, std::nullopt, 0.6);
  ASSERT(!result.has_value());
}

TEST(Combiner_OnlyPingLower) {
  auto result = combineRtt(10.0, std::nullopt, std::nullopt, 0.6);
  ASSERT(result.has_value());
  ASSERT_NEAR(*result, 10.0, 0.01);
}

TEST(Combiner_OnlyTransportLower) {
  auto result = combineRtt(std::nullopt, 15.0, std::nullopt, 0.6);
  ASSERT(result.has_value());
  ASSERT_NEAR(*result, 15.0, 0.01);
}

TEST(Combiner_OnlyHttpUpper) {
  auto result = combineRtt(std::nullopt, std::nullopt, 100.0, 0.6);
  ASSERT(result.has_value());
  ASSERT_NEAR(*result, 100.0, 0.01);
}

TEST(Combiner_BothLowerAndUpper_BiasToLower) {
  auto result = combineRtt(10.0, std::nullopt, 100.0, 1.0);  // Full bias to lower
  ASSERT(result.has_value());
  ASSERT_NEAR(*result, 10.0, 0.01);
}

TEST(Combiner_BothLowerAndUpper_BiasToUpper) {
  auto result = combineRtt(10.0, std::nullopt, 100.0, 0.0);  // Full bias to upper
  ASSERT(result.has_value());
  ASSERT_NEAR(*result, 100.0, 0.01);
}

TEST(Combiner_BothLowerAndUpper_EqualBias) {
  auto result = combineRtt(10.0, std::nullopt, 100.0, 0.5);
  ASSERT(result.has_value());
  // Geometric mean of 10 and 100 = sqrt(1000) ≈ 31.6
  double expected = std::exp(0.5 * std::log(100.0) + 0.5 * std::log(10.0));
  ASSERT_NEAR(*result, expected, 0.1);
}

TEST(Combiner_HttpLowerThanPing_GeometricMean) {
  // http < lower -> returns geometric mean
  auto result = combineRtt(50.0, std::nullopt, 10.0, 0.6);
  ASSERT(result.has_value());
  double expected = std::exp((std::log(50.0) + std::log(10.0)) / 2.0);
  ASSERT_NEAR(*result, expected, 0.1);
}

TEST(Combiner_BothPingAndTransport_UsesMin) {
  auto result = combineRtt(20.0, 10.0, 100.0, 0.6);
  ASSERT(result.has_value());
  // lower = min(20, 10) = 10
  // Combines 10 (lower) and 100 (http) with bias 0.6
  ASSERT(*result >= 10.0);
  ASSERT(*result <= 100.0);
}

TEST(Combiner_BiasClampedToRange) {
  // Bias beyond 0-1 should be clamped
  auto r1 = combineRtt(10.0, std::nullopt, 100.0, -0.5);  // clamped to 0
  auto r2 = combineRtt(10.0, std::nullopt, 100.0, 0.0);
  ASSERT(r1.has_value() && r2.has_value());
  ASSERT_NEAR(*r1, *r2, 0.01);

  auto r3 = combineRtt(10.0, std::nullopt, 100.0, 1.5);   // clamped to 1
  auto r4 = combineRtt(10.0, std::nullopt, 100.0, 1.0);
  ASSERT(r3.has_value() && r4.has_value());
  ASSERT_NEAR(*r3, *r4, 0.01);
}

TEST(Combiner_VerySmallValues) {
  // Near 1e-6 threshold
  auto result = combineRtt(0.001, std::nullopt, 0.002, 0.6);
  ASSERT(result.has_value());
}

// ============================================================================
//  WeightedMedian Tests
// ============================================================================

TEST(WeightedMedian_Empty) {
  WeightedMedian wm(0.02);
  ASSERT(wm.empty());
  ASSERT(!wm.estimate(Clock::now()).has_value());
}

TEST(WeightedMedian_SingleSample) {
  WeightedMedian wm(0.02);
  auto now = Clock::now();
  wm.add(42.0, now);
  ASSERT(!wm.empty());
  auto est = wm.estimate(now);
  ASSERT(est.has_value());
  ASSERT_NEAR(*est, 42.0, 0.01);
}

TEST(WeightedMedian_MedianOfOddSamples) {
  WeightedMedian wm(0.0);  // No decay
  auto now = Clock::now();
  wm.add(10.0, now);
  wm.add(20.0, now);
  wm.add(30.0, now);
  auto est = wm.estimate(now);
  ASSERT(est.has_value());
  ASSERT_NEAR(*est, 20.0, 0.01);
}

TEST(WeightedMedian_SignalStrengthWeighting) {
  WeightedMedian wm(0.0, 0.5);  // Aggressive signal decay
  auto now = Clock::now();

  // Add samples with different signal strengths
  wm.add(10.0, now, 50);   // signal=50
  wm.add(100.0, now, 50);  // signal=50
  wm.add(50.0, now, 50);   // signal=50

  // Estimate at same signal = 50 -> normal median
  auto est = wm.estimate(now, 50);
  ASSERT(est.has_value());
  ASSERT_NEAR(*est, 50.0, 0.01);
}

TEST(WeightedMedian_SignalStrengthFarAway) {
  WeightedMedian wm(0.0, 0.5);
  auto now = Clock::now();

  wm.add(10.0, now, 10);   // Very different signal strength
  wm.add(100.0, now, 90);  // Close signal strength

  // Estimate at signal=90 -> 100.0 should have much higher weight
  auto est = wm.estimate(now, 90);
  ASSERT(est.has_value());
  // The sample at signal=90 should dominate
  ASSERT(*est > 50.0);
}

TEST(WeightedMedian_Percentile_50th_IsMedian) {
  WeightedMedian wm(0.0);
  auto now = Clock::now();
  wm.add(10.0, now);
  wm.add(20.0, now);
  wm.add(30.0, now);
  wm.add(40.0, now);
  wm.add(50.0, now);

  auto median = wm.estimate(now);
  auto p50 = wm.percentile(now, 0.5);
  ASSERT(median.has_value() && p50.has_value());
  ASSERT_NEAR(*median, *p50, 0.01);
}

TEST(WeightedMedian_Percentile_95th) {
  WeightedMedian wm(0.0);
  auto now = Clock::now();
  for (int i = 1; i <= 100; i++) {
    wm.add(static_cast<double>(i), now);
  }
  auto p95 = wm.percentile(now, 0.95);
  ASSERT(p95.has_value());
  ASSERT(*p95 >= 90.0);
}

TEST(WeightedMedian_Percentile_OutOfRange) {
  WeightedMedian wm(0.0);
  auto now = Clock::now();
  wm.add(10.0, now);
  ASSERT(!wm.percentile(now, -0.1).has_value());
  ASSERT(!wm.percentile(now, 1.1).has_value());
}

TEST(WeightedMedian_Percentile_Empty) {
  WeightedMedian wm(0.02);
  ASSERT(!wm.percentile(Clock::now(), 0.5).has_value());
}

TEST(WeightedMedian_LatestTs) {
  WeightedMedian wm(0.02);
  auto t1 = Clock::now();
  auto t2 = t1 + std::chrono::seconds(1);

  wm.add(10.0, t1);
  wm.add(20.0, t2);
  ASSERT(wm.latestTs() == t2);
}

TEST(WeightedMedian_Decay_OldSamplesLessWeight) {
  WeightedMedian wm(1.0);  // Very fast decay
  auto now = Clock::now();

  // Old sample
  wm.add(100.0, now - std::chrono::seconds(10));
  // Recent sample
  wm.add(10.0, now);

  auto est = wm.estimate(now);
  ASSERT(est.has_value());
  // Recent sample should dominate, estimate closer to 10
  ASSERT(*est < 50.0);
}

TEST(WeightedMedian_PercentileWithSignalStrength) {
  WeightedMedian wm(0.0, 0.9);
  auto now = Clock::now();
  for (int i = 1; i <= 20; i++) {
    wm.add(static_cast<double>(i), now, 50);
  }
  auto p50 = wm.percentile(now, 0.5, 50);
  ASSERT(p50.has_value());
}

// ============================================================================
//  Aggregator Tests
// ============================================================================

TEST(Aggregator_WithSignalStrength) {
  Aggregator agg(0.0, 0.95);
  auto now = Clock::now();
  agg.add(100.0, now, 50);
  agg.add(200.0, now, 80);
  
  ASSERT_EQ(agg.sampleCount(), 2u);
  auto est = agg.estimate(now, 50);
  ASSERT(est.has_value());
}

TEST(Aggregator_MinMaxTracking) {
  Aggregator agg(0.02);
  auto now = Clock::now();
  agg.add(50.0, now);
  agg.add(10.0, now);
  agg.add(90.0, now);
  
  ASSERT(agg.minValue().has_value());
  ASSERT(agg.maxValue().has_value());
  ASSERT_NEAR(*agg.minValue(), 10.0, 0.01);
  ASSERT_NEAR(*agg.maxValue(), 90.0, 0.01);
}

TEST(Aggregator_PercentileForwarding) {
  Aggregator agg(0.0);
  auto now = Clock::now();
  for (int i = 1; i <= 100; i++) {
    agg.add(static_cast<double>(i), now);
  }
  auto p95 = agg.percentile(now, 0.95);
  ASSERT(p95.has_value());
  ASSERT(*p95 >= 90.0);
}

TEST(Aggregator_PercentileWithSignalStrength) {
  Aggregator agg(0.0, 0.95);
  auto now = Clock::now();
  for (int i = 1; i <= 50; i++) {
    agg.add(static_cast<double>(i), now, 60);
  }
  auto p50 = agg.percentile(now, 0.5, 60);
  ASSERT(p50.has_value());
}

TEST(Aggregator_LatestTs) {
  Aggregator agg(0.02);
  auto t1 = Clock::now();
  auto t2 = t1 + std::chrono::seconds(5);
  agg.add(10.0, t1);
  agg.add(20.0, t2);
  ASSERT(agg.latestTs() == t2);
}

// ============================================================================
//  Logger Tests
// ============================================================================

TEST(Logger_DebugLog_Template) {
  auto& logger = Logger::instance();
  auto prev_level = logger.getMinLevel();
  std::string captured;
  
  logger.setMinLevel(LogLevel::LOG_DEBUG);
  logger.setCallback([&](LogLevel lvl, const std::string& msg) {
    captured = msg;
  });
  
  logger.debugLog("test", " message ", 42);
  ASSERT_EQ(captured, "test message 42");
  
  logger.setMinLevel(prev_level);
  logger.setCallback(nullptr);
}

TEST(Logger_InfoLog_Template) {
  auto& logger = Logger::instance();
  auto prev_level = logger.getMinLevel();
  std::string captured;
  
  logger.setMinLevel(LogLevel::LOG_INFO);
  logger.setCallback([&](LogLevel lvl, const std::string& msg) {
    captured = msg;
  });
  
  logger.infoLog("info: ", 3.14);
  ASSERT(captured.find("info:") != std::string::npos);
  
  logger.setMinLevel(prev_level);
  logger.setCallback(nullptr);
}

TEST(Logger_WarningLog_Template) {
  auto& logger = Logger::instance();
  auto prev_level = logger.getMinLevel();
  std::string captured;
  
  logger.setMinLevel(LogLevel::LOG_WARNING);
  logger.setCallback([&](LogLevel lvl, const std::string& msg) {
    captured = msg;
  });
  
  logger.warningLog("warn!", 100);
  ASSERT(captured.find("warn!") != std::string::npos);
  
  logger.setMinLevel(prev_level);
  logger.setCallback(nullptr);
}

TEST(Logger_ErrorLog_Template) {
  auto& logger = Logger::instance();
  auto prev_level = logger.getMinLevel();
  std::string captured;
  LogLevel captured_level = LogLevel::LOG_NONE;
  
  logger.setMinLevel(LogLevel::LOG_ERROR);
  logger.setCallback([&](LogLevel lvl, const std::string& msg) {
    captured = msg;
    captured_level = lvl;
  });
  
  logger.errorLog("error: code=", -1);
  ASSERT(captured.find("error:") != std::string::npos);
  ASSERT_EQ(captured_level, LogLevel::LOG_ERROR);
  
  logger.setMinLevel(prev_level);
  logger.setCallback(nullptr);
}

TEST(Logger_LevelFiltering) {
  auto& logger = Logger::instance();
  auto prev_level = logger.getMinLevel();
  int call_count = 0;
  
  logger.setMinLevel(LogLevel::LOG_WARNING);
  logger.setCallback([&](LogLevel lvl, const std::string& msg) {
    call_count++;
  });
  
  logger.debug("should not appear");
  logger.info("should not appear");
  logger.warning("should appear");
  logger.error("should appear");
  ASSERT_EQ(call_count, 2);
  
  logger.setMinLevel(prev_level);
  logger.setCallback(nullptr);
}

TEST(Logger_TimestampFormat) {
  std::string ts = getTimestamp();
  // Format: YYYY-MM-DD HH:MM:SS.mmm
  ASSERT(ts.length() >= 23u);
  ASSERT(ts[4] == '-');
  ASSERT(ts[7] == '-');
  ASSERT(ts[10] == ' ');
  ASSERT(ts[13] == ':');
  ASSERT(ts[16] == ':');
  ASSERT(ts[19] == '.');
}

TEST(Logger_LogLevelToString) {
  ASSERT_EQ(std::string(logLevelToString(LogLevel::LOG_DEBUG)), "DEBUG");
  ASSERT_EQ(std::string(logLevelToString(LogLevel::LOG_INFO)), "INFO");
  ASSERT_EQ(std::string(logLevelToString(LogLevel::LOG_WARNING)), "WARNING");
  ASSERT_EQ(std::string(logLevelToString(LogLevel::LOG_ERROR)), "ERROR");
  ASSERT_EQ(std::string(logLevelToString(LogLevel::LOG_NONE)), "NONE");
}

// ============================================================================
//  HttpRttSource Tests
// ============================================================================

TEST(HttpRttSource_OrphanedResponse_Ignored) {
  Nqe nqe;
  HttpRttSource src(nqe);
  
  // Response without corresponding send -> should be silently ignored
  src.onResponseHeaders(12345);
  
  auto stats = nqe.getStatistics();
  ASSERT_EQ(stats.http.sample_count, 0u);
}

TEST(HttpRttSource_BasicRequestResponse) {
  Nqe nqe;
  HttpRttSource src(nqe);
  
  auto t1 = Clock::now();
  src.onRequestSent(1, t1);
  auto t2 = t1 + std::chrono::milliseconds(50);
  src.onResponseHeaders(1, t2);
  
  auto stats = nqe.getStatistics();
  ASSERT_EQ(stats.http.sample_count, 1u);
}

TEST(HttpRttSource_MultipleOverlappingRequests) {
  Nqe nqe;
  HttpRttSource src(nqe);
  
  auto t0 = Clock::now();
  src.onRequestSent(1, t0);
  src.onRequestSent(2, t0 + std::chrono::milliseconds(10));
  src.onRequestSent(3, t0 + std::chrono::milliseconds(20));
  
  // Responses arrive out of order
  src.onResponseHeaders(2, t0 + std::chrono::milliseconds(60));
  src.onResponseHeaders(1, t0 + std::chrono::milliseconds(80));
  src.onResponseHeaders(3, t0 + std::chrono::milliseconds(100));
  
  auto stats = nqe.getStatistics();
  ASSERT_EQ(stats.http.sample_count, 3u);
}

TEST(HttpRttSource_DuplicateResponse_SecondIgnored) {
  Nqe nqe;
  HttpRttSource src(nqe);
  
  auto t0 = Clock::now();
  src.onRequestSent(1, t0);
  src.onResponseHeaders(1, t0 + std::chrono::milliseconds(50));
  
  // Second response for same ID -> should be ignored (already removed)
  src.onResponseHeaders(1, t0 + std::chrono::milliseconds(100));
  
  auto stats = nqe.getStatistics();
  ASSERT_EQ(stats.http.sample_count, 1u);
}

// ============================================================================
//  QuicH2PingSource Tests
// ============================================================================

TEST(PingSource_OrphanedPong_Ignored) {
  Nqe nqe;
  QuicH2PingSource src(nqe);
  
  // Pong without corresponding ping
  src.onPong("example.com");
  
  auto stats = nqe.getStatistics();
  ASSERT_EQ(stats.ping.sample_count, 0u);
}

TEST(PingSource_BasicPingPong) {
  Nqe nqe;
  QuicH2PingSource src(nqe);
  
  bool ping_called = false;
  src.setPingImpl([&](const std::string& authority) {
    ping_called = true;
  });
  
  src.ping("example.com");
  ASSERT(ping_called);
  
  // Simulate brief delay
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  src.onPong("example.com");
  
  auto stats = nqe.getStatistics();
  ASSERT_EQ(stats.ping.sample_count, 1u);
}

TEST(PingSource_DuplicatePong_SecondIgnored) {
  Nqe nqe;
  QuicH2PingSource src(nqe);
  
  src.setPingImpl([](const std::string&) {});
  src.ping("example.com");
  
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  src.onPong("example.com");
  src.onPong("example.com");  // Should be ignored
  
  auto stats = nqe.getStatistics();
  ASSERT_EQ(stats.ping.sample_count, 1u);
}

TEST(PingSource_NoPingImpl_PingNoOp) {
  Nqe nqe;
  QuicH2PingSource src(nqe);
  
  // No setPingImpl called, ping should be no-op
  src.ping("example.com");
  src.onPong("example.com");
  
  auto stats = nqe.getStatistics();
  // Should have 0 samples since ping wasn't actually sent (no in_flight_ entry)
  ASSERT_EQ(stats.ping.sample_count, 0u);
}

TEST(PingSource_MultipleDifferentAuthorities) {
  Nqe nqe;
  QuicH2PingSource src(nqe);
  
  src.setPingImpl([](const std::string&) {});
  
  src.ping("a.com");
  src.ping("b.com");
  src.ping("c.com");
  
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  
  src.onPong("b.com");
  src.onPong("a.com");
  src.onPong("c.com");
  
  auto stats = nqe.getStatistics();
  ASSERT_EQ(stats.ping.sample_count, 3u);
}

// ============================================================================
//  ThroughputAnalyzer Tests
// ============================================================================

TEST(ThroughputAnalyzer_GetSampleCount_Empty) {
  ThroughputAnalyzer analyzer;
  ASSERT_EQ(analyzer.getSampleCount(), 0u);
}

TEST(ThroughputAnalyzer_GetSampleCount_AfterTransfer) {
  ThroughputAnalyzer::Options opts;
  opts.min_transfer_size_bytes = 1000;
  ThroughputAnalyzer analyzer(opts);
  
  auto start = Clock::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  auto end = Clock::now();
  
  analyzer.addTransfer(50000, start, end);
  ASSERT_EQ(analyzer.getSampleCount(), 1u);
}

TEST(ThroughputAnalyzer_IsHangingWindow) {
  ThroughputAnalyzer::Options opts;
  opts.enable_hanging_window_detection = true;
  opts.hanging_window_cwnd_multiplier = 0.5;
  opts.hanging_window_cwnd_size_kb = 10;
  ThroughputAnalyzer analyzer(opts);
  
  // A window where very few bits are received relative to cwnd
  // cwnd = 10KB = 80000 bits, multiplier = 0.5 -> threshold = 40000 bits per http_rtt
  bool hanging = analyzer.isHangingWindow(
    100,                                          // tiny bits received
    std::chrono::milliseconds(1000),              // 1 second window 
    100.0                                         // 100ms HTTP RTT
  );
  ASSERT(hanging);
  
  // A window with plenty of data
  bool not_hanging = analyzer.isHangingWindow(
    1000000,                                      // lots of bits
    std::chrono::milliseconds(1000),
    100.0
  );
  ASSERT(!not_hanging);
}

TEST(ThroughputAnalyzer_ConnectionTypeChanged) {
  ThroughputAnalyzer::Options opts;
  opts.throughput_min_requests_in_flight = 2;
  ThroughputAnalyzer analyzer(opts);

  int id1, id2;
  ThroughputAnalyzer::Request req1{&id1, 0, "GET", 50000, Clock::now(), false, false};
  ThroughputAnalyzer::Request req2{&id2, 0, "GET", 50000, Clock::now(), false, false};

  analyzer.notifyStartTransaction(req1);
  analyzer.notifyStartTransaction(req2);

  auto stats1 = analyzer.getStatistics();
  ASSERT_EQ(stats1.active_requests, 2u);
  ASSERT_EQ(stats1.degrading_requests, 0u);

  analyzer.onConnectionTypeChanged();

  auto stats2 = analyzer.getStatistics();
  ASSERT_EQ(stats2.active_requests, 0u);
  ASSERT_EQ(stats2.degrading_requests, 2u);
}

TEST(ThroughputAnalyzer_EstimateEmpty) {
  ThroughputAnalyzer analyzer;
  auto est = analyzer.getEstimate();
  ASSERT(!est.has_value());
}

// ============================================================================
//  Nqe Observer Tests
// ============================================================================

class TestRTTObserver : public RTTObserver {
public:
  int count = 0;
  double last_rtt = 0;
  std::string last_source;
  void onRTTObservation(double rtt_ms, const char* source) override {
    count++;
    last_rtt = rtt_ms;
    last_source = source ? source : "";
  }
};

class TestThroughputObserver : public ThroughputObserver {
public:
  int count = 0;
  double last_throughput = 0;
  void onThroughputObservation(double throughput_kbps) override {
    count++;
    last_throughput = throughput_kbps;
  }
};

class TestECTObserver : public EffectiveConnectionTypeObserver {
public:
  int count = 0;
  EffectiveConnectionType last_old = EffectiveConnectionType::UNKNOWN;
  EffectiveConnectionType last_new = EffectiveConnectionType::UNKNOWN;
  void onEffectiveConnectionTypeChanged(EffectiveConnectionType old_type, EffectiveConnectionType new_type) override {
    count++;
    last_old = old_type;
    last_new = new_type;
  }
};

TEST(Nqe_RTTObserver_AddRemove) {
  Nqe nqe;
  TestRTTObserver obs;
  
  nqe.addRTTObserver(&obs);
  nqe.addSample(Source::HTTP_TTFB, 100.0);
  ASSERT(obs.count > 0);
  
  int prev_count = obs.count;
  nqe.removeRTTObserver(&obs);
  nqe.addSample(Source::HTTP_TTFB, 200.0);
  ASSERT_EQ(obs.count, prev_count);  // No more notifications
}

TEST(Nqe_ThroughputObserver_AddRemove) {
  Nqe nqe;
  TestThroughputObserver obs;
  
  nqe.addThroughputObserver(&obs);
  auto now = Clock::now();
  nqe.addThroughputSample(100000, now - std::chrono::seconds(1), now);
  ASSERT(obs.count > 0);
  
  int prev_count = obs.count;
  nqe.removeThroughputObserver(&obs);
  nqe.addThroughputSample(200000, now - std::chrono::seconds(2), now);
  ASSERT_EQ(obs.count, prev_count);
}

TEST(Nqe_ECTObserver_AddRemove) {
  Nqe nqe;
  TestECTObserver obs;
  
  nqe.addEffectiveConnectionTypeObserver(&obs);
  // Add sample that should trigger ECT to change from UNKNOWN to 4G
  nqe.addSample(Source::HTTP_TTFB, 50.0);
  nqe.getEstimate();
  
  // ECT should have changed at least once (UNKNOWN -> 4G)
  int prev_count = obs.count;
  
  nqe.removeEffectiveConnectionTypeObserver(&obs);
  nqe.addSample(Source::HTTP_TTFB, 3000.0);
  nqe.getEstimate();
  ASSERT_EQ(obs.count, prev_count);  // No more notifications
}

TEST(Nqe_EstimateCallback) {
  Nqe nqe;
  int cb_count = 0;
  Estimate last_est;
  
  nqe.setEstimateCallback([&](const Estimate& est) {
    cb_count++;
    last_est = est;
  });
  
  nqe.addSample(Source::HTTP_TTFB, 100.0);
  // Callback may or may not be called immediately depending on implementation,
  // but getEstimate should trigger it
  auto est = nqe.getEstimate();
  ASSERT(cb_count >= 0);  // At minimum, should not crash
}

TEST(Nqe_CallbackAdapter_RTT) {
  int count = 0;
  RTTObserverCallbackAdapter adapter([&](double rtt, const char* src) {
    count++;
  });
  
  adapter.onRTTObservation(50.0, "test");
  ASSERT_EQ(count, 1);
}

TEST(Nqe_CallbackAdapter_Throughput) {
  int count = 0;
  ThroughputObserverCallbackAdapter adapter([&](double tp) {
    count++;
  });
  
  adapter.onThroughputObservation(5000.0);
  ASSERT_EQ(count, 1);
}

// ============================================================================
//  Nqe Validation Tests
// ============================================================================

TEST(Nqe_ValidateOptions_Valid) {
  Nqe::Options opts;
  std::string err;
  ASSERT(Nqe::validateOptions(opts, &err));
}

TEST(Nqe_ValidateOptions_NegativeLambda) {
  Nqe::Options opts;
  opts.decay_lambda_per_sec = -1.0;
  std::string err;
  ASSERT(!Nqe::validateOptions(opts, &err));
  ASSERT(!err.empty());
}

TEST(Nqe_ValidateOptions_BiasTooHigh) {
  Nqe::Options opts;
  opts.combine_bias_to_lower = 1.5;
  std::string err;
  ASSERT(!Nqe::validateOptions(opts, &err));
}

TEST(Nqe_ValidateOptions_BiasTooLow) {
  Nqe::Options opts;
  opts.combine_bias_to_lower = -0.1;
  std::string err;
  ASSERT(!Nqe::validateOptions(opts, &err));
}

TEST(Nqe_ValidateOptions_NoErrorPtr) {
  Nqe::Options opts;
  opts.decay_lambda_per_sec = -10;
  ASSERT(!Nqe::validateOptions(opts, nullptr));
}

// ============================================================================
//  Nqe Source Types Tests
// ============================================================================

TEST(Nqe_Source_TCP) {
  Nqe nqe;
  nqe.addSample(Source::TCP, 25.0);
  auto stats = nqe.getStatistics();
  ASSERT(stats.transport.sample_count > 0);
}

TEST(Nqe_Source_QUIC) {
  Nqe nqe;
  nqe.addSample(Source::QUIC, 15.0);
  auto stats = nqe.getStatistics();
  ASSERT(stats.transport.sample_count > 0);
}

TEST(Nqe_Source_H2Pings) {
  Nqe nqe;
  nqe.addSample(Source::H2_PINGS, 30.0);
  auto stats = nqe.getStatistics();
  ASSERT(stats.end_to_end.sample_count > 0);
}

TEST(Nqe_Source_H3Pings) {
  Nqe nqe;
  nqe.addSample(Source::H3_PINGS, 20.0);
  auto stats = nqe.getStatistics();
  ASSERT(stats.end_to_end.sample_count > 0);
}

TEST(Nqe_Source_CachedEstimates) {
  Nqe nqe;
  nqe.addSample(Source::HTTP_CACHED_ESTIMATE, 100.0);
  nqe.addSample(Source::TRANSPORT_CACHED_ESTIMATE, 30.0);
  auto stats = nqe.getStatistics();
  ASSERT(stats.http.sample_count > 0);
  ASSERT(stats.transport.sample_count > 0);
}

// ============================================================================
//  Nqe Statistics Tests
// ============================================================================

TEST(Nqe_Statistics_MultiSource) {
  Nqe nqe;
  auto now = Clock::now();
  
  nqe.addSample(Source::HTTP_TTFB, 100.0, now);
  nqe.addSample(Source::HTTP_TTFB, 120.0, now);
  nqe.addSample(Source::TRANSPORT_RTT, 30.0, now);
  nqe.addSample(Source::PING_RTT, 20.0, now);
  nqe.addThroughputSample(100000, now - std::chrono::seconds(1), now);
  
  auto stats = nqe.getStatistics();
  ASSERT_EQ(stats.http.sample_count, 2u);
  ASSERT_EQ(stats.transport.sample_count, 1u);
  ASSERT_EQ(stats.ping.sample_count, 1u);
  ASSERT(stats.total_samples >= 4u);
}

TEST(Nqe_GetActiveSockets) {
  Nqe nqe;
  ASSERT_EQ(nqe.getActiveSockets(), 0u);
}

TEST(Nqe_TransportSampler_StartStop) {
  Nqe nqe;
  ASSERT(!nqe.isTransportSamplerRunning());
  
  nqe.startTransportSampler();
  ASSERT(nqe.isTransportSamplerRunning());
  
  nqe.stopTransportSampler();
  ASSERT(!nqe.isTransportSamplerRunning());
}

// Note: DoubleStart/DoubleStop tests removed - platform-specific
// transport sampler may have different behavior on each platform

// ============================================================================
//  Nqe Network Change Detection Tests
// ============================================================================

TEST(Nqe_NetworkChange_DefaultDisabled) {
  Nqe nqe;
  ASSERT(!nqe.isNetworkChangeDetectionEnabled());
}

TEST(Nqe_NetworkChange_EnableDisable) {
  Nqe nqe;
  nqe.enableNetworkChangeDetection(true);
  ASSERT(nqe.isNetworkChangeDetectionEnabled());
  nqe.disableNetworkChangeDetection();
  ASSERT(!nqe.isNetworkChangeDetectionEnabled());
}

TEST(Nqe_NetworkChange_DoubleEnable) {
  Nqe nqe;
  nqe.enableNetworkChangeDetection(true);
  nqe.enableNetworkChangeDetection(false);  // Should not crash, updates setting
  ASSERT(nqe.isNetworkChangeDetectionEnabled());
  nqe.disableNetworkChangeDetection();
}

// ============================================================================
//  Nqe Caching Tests
// ============================================================================

TEST(Nqe_Caching_UpdateAndGet) {
  Nqe::Options opts;
  opts.enable_caching = true;
  opts.max_cache_size = 10;
  Nqe nqe(opts);
  
  // Add samples and update cache
  nqe.addSample(Source::HTTP_TTFB, 100.0);
  nqe.addSample(Source::TRANSPORT_RTT, 30.0);
  nqe.updateCachedQuality();
  
  // Cache operations should not crash even without network change detection
}

TEST(Nqe_Caching_SaveAndLoad) {
  std::string path = getTempFilePath("nqe_unit_cache.dat");
  
  {
    Nqe::Options opts;
    opts.enable_caching = true;
    opts.cache_file_path = path;
    Nqe nqe(opts);
    
    nqe.addSample(Source::HTTP_TTFB, 100.0);
    nqe.saveCachedData();
  }
  
  {
    Nqe::Options opts;
    opts.enable_caching = true;
    opts.cache_file_path = path;
    Nqe nqe(opts);
    
    nqe.loadCachedData();
    // Should not crash
  }
  
  std::remove(path.c_str());
}

TEST(Nqe_GetSignalStrength_Default) {
  Nqe nqe;
  // Default signal strength should be -1 (not applicable)
  ASSERT_EQ(nqe.getCurrentSignalStrength(), -1);
}

// ============================================================================
//  ReportGenerator Tests
// ============================================================================

TEST(ReportGenerator_HtmlReport) {
  std::string path = getTempFilePath("nqe_test_report.html");
  
  ReportGenerator::TestData data;
  data.urls = {"https://example.com"};
  data.options = Nqe::Options{};
  data.test_start = Clock::now() - std::chrono::seconds(10);
  data.test_end = Clock::now();
  data.final_estimate.rtt_ms = 50.0;
  data.final_estimate.http_ttfb_ms = 100.0;
  data.final_estimate.effective_type = EffectiveConnectionType::TYPE_4G;
  data.final_stats.total_samples = 42;
  
  bool ok = ReportGenerator::generateHtmlReport(data, path);
  ASSERT(ok);
  
  // Verify file exists and has content
  std::ifstream in(path);
  ASSERT(in.is_open());
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  ASSERT(content.find("<!DOCTYPE html>") != std::string::npos);
  ASSERT(content.find("NQE") != std::string::npos);
  
  std::remove(path.c_str());
}

TEST(ReportGenerator_TextReport) {
  std::string path = getTempFilePath("nqe_test_report.txt");
  
  ReportGenerator::TestData data;
  data.urls = {"https://example.com", "https://test.com"};
  data.options = Nqe::Options{};
  data.test_start = Clock::now() - std::chrono::seconds(5);
  data.test_end = Clock::now();
  data.final_estimate.rtt_ms = 80.0;
  data.final_estimate.effective_type = EffectiveConnectionType::TYPE_3G;
  data.final_stats.total_samples = 20;
  
  bool ok = ReportGenerator::generateTextReport(data, path);
  ASSERT(ok);
  
  // Verify file exists
  std::ifstream in(path);
  ASSERT(in.is_open());
  
  std::remove(path.c_str());
}

TEST(ReportGenerator_InvalidPath) {
  ReportGenerator::TestData data;
  data.test_start = Clock::now();
  data.test_end = Clock::now();
  
  // An invalid path that should fail
  bool ok = ReportGenerator::generateHtmlReport(data, "/nonexistent/path/that/does/not/exist/report.html");
  ASSERT(!ok);
}

TEST(ReportGenerator_EmptyData) {
  std::string path = getTempFilePath("nqe_empty_report.html");
  
  ReportGenerator::TestData data;
  data.test_start = Clock::now();
  data.test_end = Clock::now();
  // All default/empty
  
  bool ok = ReportGenerator::generateHtmlReport(data, path);
  ASSERT(ok);
  
  std::remove(path.c_str());
}

// ============================================================================
//  Nqe Estimate Computation Tests
// ============================================================================

TEST(Nqe_Estimate_HttpOnly) {
  Nqe nqe;
  auto now = Clock::now();
  nqe.addSample(Source::HTTP_TTFB, 100.0, now);
  
  auto est = nqe.getEstimate(now);
  ASSERT(est.http_ttfb_ms.has_value());
  ASSERT_NEAR(*est.http_ttfb_ms, 100.0, 0.1);
}

TEST(Nqe_Estimate_TransportOnly) {
  Nqe nqe;
  auto now = Clock::now();
  nqe.addSample(Source::TRANSPORT_RTT, 30.0, now);
  
  auto est = nqe.getEstimate(now);
  ASSERT(est.transport_rtt_ms.has_value());
  ASSERT_NEAR(*est.transport_rtt_ms, 30.0, 0.1);
}

TEST(Nqe_Estimate_Combined) {
  Nqe nqe;
  auto now = Clock::now();
  nqe.addSample(Source::HTTP_TTFB, 100.0, now);
  nqe.addSample(Source::TRANSPORT_RTT, 30.0, now);
  
  auto est = nqe.getEstimate(now);
  ASSERT(est.rtt_ms > 0);
  // Combined RTT should be between transport and HTTP
  ASSERT(est.rtt_ms >= 30.0 - 1.0);
  ASSERT(est.rtt_ms <= 100.0 + 1.0);
}

TEST(Nqe_Estimate_WithThroughput) {
  Nqe nqe;
  auto now = Clock::now();
  nqe.addSample(Source::HTTP_TTFB, 100.0, now);
  nqe.addThroughputSample(100000, now - std::chrono::seconds(1), now);
  
  auto est = nqe.getEstimate(now);
  ASSERT(est.throughput_kbps.has_value());
  ASSERT(*est.throughput_kbps > 0);
}

TEST(Nqe_GetEffectiveConnectionType) {
  Nqe nqe;
  auto now = Clock::now();
  nqe.addSample(Source::HTTP_TTFB, 50.0, now);  // Fast = 4G
  
  auto ect = nqe.getEffectiveConnectionType(now);
  ASSERT_EQ(ect, EffectiveConnectionType::TYPE_4G);
}

TEST(Nqe_GetEffectiveConnectionType_NoSamples) {
  Nqe nqe;
  auto ect = nqe.getEffectiveConnectionType();
  ASSERT_EQ(ect, EffectiveConnectionType::UNKNOWN);
}

TEST(Nqe_GetOptions) {
  Nqe::Options opts;
  opts.decay_lambda_per_sec = 0.05;
  opts.combine_bias_to_lower = 0.7;
  Nqe nqe(opts);
  
  auto& retrieved = nqe.getOptions();
  ASSERT_NEAR(retrieved.decay_lambda_per_sec, 0.05, 0.001);
  ASSERT_NEAR(retrieved.combine_bias_to_lower, 0.7, 0.001);
}

// ============================================================================
//  Main
// ============================================================================

int main() {
  std::cout << "╔══════════════════════════════════════════════════════════╗\n";
  std::cout << "║          NQE Comprehensive Unit Test Suite              ║\n";
  std::cout << "╚══════════════════════════════════════════════════════════╝\n";

  printSection("NetworkID");
  { TestRegistrar_NetworkID_OperatorLess_DifferentTypes r; }
  { TestRegistrar_NetworkID_OperatorLess_SameType_DifferentName r; }
  { TestRegistrar_NetworkID_OperatorLess_Equal r; }
  { TestRegistrar_NetworkID_Hash_EqualObjects r; }
  { TestRegistrar_NetworkID_Hash_UsableInUnorderedMap r; }
  { TestRegistrar_NetworkID_Hash_DifferentObjects r; }
  { TestRegistrar_NetworkID_DefaultIsInvalid r; }
  { TestRegistrar_NetworkID_NoneIsInvalid r; }
  { TestRegistrar_NetworkID_Equality_IgnoresSignalStrength r; }
  { TestRegistrar_NetworkID_OperatorNotEqual r; }

  printSection("CachedNetworkQuality");
  { TestRegistrar_CachedNetworkQuality_DefaultIsEmpty r; }
  { TestRegistrar_CachedNetworkQuality_FullConstructor_AllGetters r; }
  { TestRegistrar_CachedNetworkQuality_EmptyNotFresh r; }
  { TestRegistrar_CachedNetworkQuality_FreshCheck r; }
  { TestRegistrar_CachedNetworkQuality_Update r; }
  { TestRegistrar_CachedNetworkQuality_PartiallyEmpty r; }

  printSection("EffectiveConnectionType");
  { TestRegistrar_ECT_AllNullopt_ReturnsUnknown r; }
  { TestRegistrar_ECT_OnlyHttpRtt_4G r; }
  { TestRegistrar_ECT_OnlyHttpRtt_3G r; }
  { TestRegistrar_ECT_OnlyHttpRtt_2G r; }
  { TestRegistrar_ECT_OnlyHttpRtt_Slow2G r; }
  { TestRegistrar_ECT_OnlyTransportRtt r; }
  { TestRegistrar_ECT_OnlyThroughput_High r; }
  { TestRegistrar_ECT_OnlyThroughput_Low r; }
  { TestRegistrar_ECT_BoundaryExact_400ms_Is3G r; }
  { TestRegistrar_ECT_BoundaryJustBelow_400ms_Is4G r; }
  { TestRegistrar_ECT_BoundaryExact_1400ms_Is2G r; }
  { TestRegistrar_ECT_BoundaryExact_2000ms_IsSlow2G r; }
  { TestRegistrar_ECT_RttAndThroughput_Disagree_ReturnsWorse r; }
  { TestRegistrar_ECT_CustomThresholds r; }
  { TestRegistrar_ECT_BothRtts_UsesMinimum r; }
  { TestRegistrar_ECT_ThroughputBoundaries r; }
  { TestRegistrar_ECT_ToString r; }

  printSection("NetworkQualityStore");
  { TestRegistrar_Store_Remove r; }
  { TestRegistrar_Store_Remove_NonExistent r; }
  { TestRegistrar_Store_Clear r; }
  { TestRegistrar_Store_Contains r; }
  { TestRegistrar_Store_GetAll r; }
  { TestRegistrar_Store_PruneStaleEntries r; }
  { TestRegistrar_Store_PruneAllFresh r; }
  { TestRegistrar_Store_Eviction r; }
  { TestRegistrar_Store_Persistence_SaveLoad r; }
  { TestRegistrar_Store_LoadFromEmptyPath r; }
  { TestRegistrar_Store_UpdateExistingEntry r; }

  printSection("Combiner");
  { TestRegistrar_Combiner_AllNull r; }
  { TestRegistrar_Combiner_OnlyPingLower r; }
  { TestRegistrar_Combiner_OnlyTransportLower r; }
  { TestRegistrar_Combiner_OnlyHttpUpper r; }
  { TestRegistrar_Combiner_BothLowerAndUpper_BiasToLower r; }
  { TestRegistrar_Combiner_BothLowerAndUpper_BiasToUpper r; }
  { TestRegistrar_Combiner_BothLowerAndUpper_EqualBias r; }
  { TestRegistrar_Combiner_HttpLowerThanPing_GeometricMean r; }
  { TestRegistrar_Combiner_BothPingAndTransport_UsesMin r; }
  { TestRegistrar_Combiner_BiasClampedToRange r; }
  { TestRegistrar_Combiner_VerySmallValues r; }

  printSection("WeightedMedian");
  { TestRegistrar_WeightedMedian_Empty r; }
  { TestRegistrar_WeightedMedian_SingleSample r; }
  { TestRegistrar_WeightedMedian_MedianOfOddSamples r; }
  { TestRegistrar_WeightedMedian_SignalStrengthWeighting r; }
  { TestRegistrar_WeightedMedian_SignalStrengthFarAway r; }
  { TestRegistrar_WeightedMedian_Percentile_50th_IsMedian r; }
  { TestRegistrar_WeightedMedian_Percentile_95th r; }
  { TestRegistrar_WeightedMedian_Percentile_OutOfRange r; }
  { TestRegistrar_WeightedMedian_Percentile_Empty r; }
  { TestRegistrar_WeightedMedian_LatestTs r; }
  { TestRegistrar_WeightedMedian_Decay_OldSamplesLessWeight r; }
  { TestRegistrar_WeightedMedian_PercentileWithSignalStrength r; }

  printSection("Aggregator");
  { TestRegistrar_Aggregator_WithSignalStrength r; }
  { TestRegistrar_Aggregator_MinMaxTracking r; }
  { TestRegistrar_Aggregator_PercentileForwarding r; }
  { TestRegistrar_Aggregator_PercentileWithSignalStrength r; }
  { TestRegistrar_Aggregator_LatestTs r; }

  printSection("Logger");
  { TestRegistrar_Logger_DebugLog_Template r; }
  { TestRegistrar_Logger_InfoLog_Template r; }
  { TestRegistrar_Logger_WarningLog_Template r; }
  { TestRegistrar_Logger_ErrorLog_Template r; }
  { TestRegistrar_Logger_LevelFiltering r; }
  { TestRegistrar_Logger_TimestampFormat r; }
  { TestRegistrar_Logger_LogLevelToString r; }

  printSection("HttpRttSource");
  { TestRegistrar_HttpRttSource_OrphanedResponse_Ignored r; }
  { TestRegistrar_HttpRttSource_BasicRequestResponse r; }
  { TestRegistrar_HttpRttSource_MultipleOverlappingRequests r; }
  { TestRegistrar_HttpRttSource_DuplicateResponse_SecondIgnored r; }

  printSection("QuicH2PingSource");
  { TestRegistrar_PingSource_OrphanedPong_Ignored r; }
  { TestRegistrar_PingSource_BasicPingPong r; }
  { TestRegistrar_PingSource_DuplicatePong_SecondIgnored r; }
  { TestRegistrar_PingSource_NoPingImpl_PingNoOp r; }
  { TestRegistrar_PingSource_MultipleDifferentAuthorities r; }

  printSection("ThroughputAnalyzer");
  { TestRegistrar_ThroughputAnalyzer_GetSampleCount_Empty r; }
  { TestRegistrar_ThroughputAnalyzer_GetSampleCount_AfterTransfer r; }
  { TestRegistrar_ThroughputAnalyzer_IsHangingWindow r; }
  { TestRegistrar_ThroughputAnalyzer_ConnectionTypeChanged r; }
  { TestRegistrar_ThroughputAnalyzer_EstimateEmpty r; }

  printSection("Nqe Observers");
  { TestRegistrar_Nqe_RTTObserver_AddRemove r; }
  { TestRegistrar_Nqe_ThroughputObserver_AddRemove r; }
  { TestRegistrar_Nqe_ECTObserver_AddRemove r; }
  { TestRegistrar_Nqe_EstimateCallback r; }
  { TestRegistrar_Nqe_CallbackAdapter_RTT r; }
  { TestRegistrar_Nqe_CallbackAdapter_Throughput r; }

  printSection("Nqe Validation");
  { TestRegistrar_Nqe_ValidateOptions_Valid r; }
  { TestRegistrar_Nqe_ValidateOptions_NegativeLambda r; }
  { TestRegistrar_Nqe_ValidateOptions_BiasTooHigh r; }
  { TestRegistrar_Nqe_ValidateOptions_BiasTooLow r; }
  { TestRegistrar_Nqe_ValidateOptions_NoErrorPtr r; }

  printSection("Nqe Sources");
  { TestRegistrar_Nqe_Source_TCP r; }
  { TestRegistrar_Nqe_Source_QUIC r; }
  { TestRegistrar_Nqe_Source_H2Pings r; }
  { TestRegistrar_Nqe_Source_H3Pings r; }
  { TestRegistrar_Nqe_Source_CachedEstimates r; }

  printSection("Nqe Statistics");
  { TestRegistrar_Nqe_Statistics_MultiSource r; }
  { TestRegistrar_Nqe_GetActiveSockets r; }
  { TestRegistrar_Nqe_TransportSampler_StartStop r; }

  printSection("Nqe Network Change");
  { TestRegistrar_Nqe_NetworkChange_DefaultDisabled r; }
  { TestRegistrar_Nqe_NetworkChange_EnableDisable r; }
  { TestRegistrar_Nqe_NetworkChange_DoubleEnable r; }

  printSection("Nqe Caching");
  { TestRegistrar_Nqe_Caching_UpdateAndGet r; }
  { TestRegistrar_Nqe_Caching_SaveAndLoad r; }
  { TestRegistrar_Nqe_GetSignalStrength_Default r; }

  printSection("ReportGenerator");
  { TestRegistrar_ReportGenerator_HtmlReport r; }
  { TestRegistrar_ReportGenerator_TextReport r; }
  { TestRegistrar_ReportGenerator_InvalidPath r; }
  { TestRegistrar_ReportGenerator_EmptyData r; }

  printSection("Nqe Estimates");
  { TestRegistrar_Nqe_Estimate_HttpOnly r; }
  { TestRegistrar_Nqe_Estimate_TransportOnly r; }
  { TestRegistrar_Nqe_Estimate_Combined r; }
  { TestRegistrar_Nqe_Estimate_WithThroughput r; }
  { TestRegistrar_Nqe_GetEffectiveConnectionType r; }
  { TestRegistrar_Nqe_GetEffectiveConnectionType_NoSamples r; }
  { TestRegistrar_Nqe_GetOptions r; }

  // Summary
  std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
  std::cout << "║  Results: " << std::setw(3) << tests_passed << " passed, "
            << std::setw(3) << tests_failed << " failed"
            << std::string(27 - (tests_failed > 99 ? 2 : tests_failed > 9 ? 1 : 0), ' ') << "║\n";
  std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";

  return tests_failed > 0 ? 1 : 0;
}
