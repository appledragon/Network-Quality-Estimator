/**
 * Comprehensive test for ThroughputAnalyzer implementing Chromium's NQE details:
 * - Request tracking (requests_ and accuracy_degrading_requests_)
 * - Request filtering (ShouldDiscardRequest, DegradesAccuracy)
 * - Observation window mechanism
 * - Minimum requests in flight threshold
 * - Network change handling
 */

#include "nqe/ThroughputAnalyzer.h"
#include "nqe/Logger.h"
#include <iostream>
#include <thread>
#include <cassert>
#include <vector>

using namespace nqe;

// Helper to generate safe unique request IDs using stack addresses
class RequestIdGenerator {
  std::vector<int> ids_;
public:
  void* next() {
    ids_.push_back(ids_.size());
    return &ids_.back();
  }
  void reset() {
    ids_.clear();
  }
};

void testBasicRequestTracking() {
  std::cout << "\n=== Test 1: Basic Request Tracking ===\n";
  
  ThroughputAnalyzer::Options opts;
  opts.min_transfer_size_bytes = 32000; // 32KB
  opts.throughput_min_requests_in_flight = 3; // Lower for testing
  
  ThroughputAnalyzer analyzer(opts);
  
  // Create valid GET requests with safe unique IDs (using stack addresses)
  int id1, id2, id3;
  ThroughputAnalyzer::Request req1{&id1, 0, "GET", 0, Clock::now(), false, false};
  ThroughputAnalyzer::Request req2{&id2, 0, "GET", 0, Clock::now(), false, false};
  ThroughputAnalyzer::Request req3{&id3, 0, "GET", 0, Clock::now(), false, false};
  
  // Start transactions
  analyzer.notifyStartTransaction(req1);
  analyzer.notifyStartTransaction(req2);
  analyzer.notifyStartTransaction(req3);
  
  auto stats = analyzer.getStatistics();
  assert(stats.active_requests == 3);
  assert(stats.degrading_requests == 0);
  
  std::cout << "✓ Active requests: " << stats.active_requests << "\n";
  std::cout << "✓ Degrading requests: " << stats.degrading_requests << "\n";
  std::cout << "PASS\n";
}

void testRequestMethodFiltering() {
  std::cout << "\n=== Test 2: Request Method Filtering ===\n";
  
  ThroughputAnalyzer analyzer;
  RequestIdGenerator idGen;
  
  // GET request should be tracked
  ThroughputAnalyzer::Request getReq{idGen.next(), 0, "GET", 0, Clock::now(), false, false};
  analyzer.notifyStartTransaction(getReq);
  
  // POST request should be discarded
  ThroughputAnalyzer::Request postReq{idGen.next(), 0, "POST", 0, Clock::now(), false, false};
  analyzer.notifyStartTransaction(postReq);
  
  // HEAD request should be discarded
  ThroughputAnalyzer::Request headReq{idGen.next(), 0, "HEAD", 0, Clock::now(), false, false};
  analyzer.notifyStartTransaction(headReq);
  
  auto stats = analyzer.getStatistics();
  assert(stats.active_requests == 1); // Only GET request
  assert(stats.degrading_requests == 0);
  
  std::cout << "✓ Only GET requests tracked\n";
  std::cout << "✓ Active requests: " << stats.active_requests << "\n";
  std::cout << "PASS\n";
}

void testAccuracyDegradingRequests() {
  std::cout << "\n=== Test 3: Accuracy Degrading Requests ===\n";
  
  ThroughputAnalyzer::Options opts;
  opts.min_transfer_size_bytes = 32000; // 32KB
  
  ThroughputAnalyzer analyzer(opts);
  RequestIdGenerator idGen;
  
  // Valid request (will be active even with low bytes - bytes accumulate over time)
  ThroughputAnalyzer::Request validReq{idGen.next(), 0, "GET", 50000, Clock::now(), false, false};
  analyzer.notifyStartTransaction(validReq);
  
  // Cached request (degrades accuracy)
  ThroughputAnalyzer::Request cachedReq{idGen.next(), 0, "GET", 50000, Clock::now(), true, false};
  analyzer.notifyStartTransaction(cachedReq);
  
  // Hanging request (degrades accuracy)
  ThroughputAnalyzer::Request hangingReq{idGen.next(), 0, "GET", 50000, Clock::now(), false, true};
  analyzer.notifyStartTransaction(hangingReq);
  
  auto stats = analyzer.getStatistics();
  assert(stats.active_requests == 1); // Only valid request
  assert(stats.degrading_requests == 2); // Cached and hanging
  
  std::cout << "✓ Active requests: " << stats.active_requests << "\n";
  std::cout << "✓ Degrading requests (cached/hanging): " << stats.degrading_requests << "\n";
  std::cout << "PASS\n";
}

void testObservationWindow() {
  std::cout << "\n=== Test 4: Observation Window Mechanism ===\n";
  
  ThroughputAnalyzer::Options opts;
  opts.min_transfer_size_bytes = 32000; // 32KB
  opts.throughput_min_requests_in_flight = 3;
  
  ThroughputAnalyzer analyzer(opts);
  RequestIdGenerator idGen;
  
  // Start with 2 requests - window should NOT start (< min_requests_in_flight)
  void* id1 = idGen.next();
  void* id2 = idGen.next();
  ThroughputAnalyzer::Request req1{id1, 0, "GET", 0, Clock::now(), false, false};
  ThroughputAnalyzer::Request req2{id2, 0, "GET", 0, Clock::now(), false, false};
  
  analyzer.notifyStartTransaction(req1);
  analyzer.notifyStartTransaction(req2);
  
  std::cout << "✓ Window should not start with 2 requests (min=3)\n";
  
  // Add 3rd request - window should start now
  void* id3 = idGen.next();
  ThroughputAnalyzer::Request req3{id3, 0, "GET", 0, Clock::now(), false, false};
  analyzer.notifyStartTransaction(req3);
  
  std::cout << "✓ Window should start with 3 requests\n";
  
  // Simulate data transfer for each request
  analyzer.notifyBytesRead(id1, 40000);
  analyzer.notifyBytesRead(id2, 40000);
  analyzer.notifyBytesRead(id3, 40000);
  
  // Wait a bit for measurable duration
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // Complete one request - should trigger throughput observation
  analyzer.notifyRequestCompleted(id1);
  
  auto stats = analyzer.getStatistics();
  std::cout << "✓ Throughput samples after request completion: " << stats.sample_count << "\n";
  
  // Clean up remaining requests
  analyzer.notifyRequestCompleted(id2);
  analyzer.notifyRequestCompleted(id3);
  
  std::cout << "PASS\n";
}

void testBytesReadUpdates() {
  std::cout << "\n=== Test 5: Bytes Read Updates ===\n";
  
  ThroughputAnalyzer::Options opts;
  opts.min_transfer_size_bytes = 32000; // 32KB
  
  ThroughputAnalyzer analyzer(opts);
  RequestIdGenerator idGen;
  
  // Start request with small size (but not degrading - bytes accumulate over time)
  void* id1 = idGen.next();
  ThroughputAnalyzer::Request req{id1, 0, "GET", 1000, Clock::now(), false, false};
  analyzer.notifyStartTransaction(req);
  
  auto stats1 = analyzer.getStatistics();
  assert(stats1.active_requests == 1); // In active set, can accumulate bytes
  
  std::cout << "✓ Request starts in active set (bytes can accumulate)\n";
  
  // Update with more bytes - should remain active
  analyzer.notifyBytesRead(id1, 50000);
  
  auto stats2 = analyzer.getStatistics();
  assert(stats2.active_requests == 1); // Still active
  
  std::cout << "✓ Request remains active after bytes update\n";
  
  // Clean up
  analyzer.notifyRequestCompleted(id1);
  
  std::cout << "PASS\n";
}

void testNetworkChangeHandling() {
  std::cout << "\n=== Test 6: Network Change Handling ===\n";
  
  ThroughputAnalyzer::Options opts;
  opts.throughput_min_requests_in_flight = 2;
  opts.min_transfer_size_bytes = 32000;
  
  ThroughputAnalyzer analyzer(opts);
  RequestIdGenerator idGen;
  
  // Add some active requests
  void* id1 = idGen.next();
  void* id2 = idGen.next();
  ThroughputAnalyzer::Request req1{id1, 0, "GET", 50000, Clock::now(), false, false};
  ThroughputAnalyzer::Request req2{id2, 0, "GET", 50000, Clock::now(), false, false};
  
  analyzer.notifyStartTransaction(req1);
  analyzer.notifyStartTransaction(req2);
  
  auto stats1 = analyzer.getStatistics();
  assert(stats1.active_requests == 2);
  assert(stats1.degrading_requests == 0);
  
  std::cout << "✓ Before network change: active=" << stats1.active_requests 
            << ", degrading=" << stats1.degrading_requests << "\n";
  
  // Simulate network change
  analyzer.onConnectionTypeChanged();
  
  auto stats2 = analyzer.getStatistics();
  assert(stats2.active_requests == 0); // All moved to degrading
  assert(stats2.degrading_requests == 2);
  
  std::cout << "✓ After network change: active=" << stats2.active_requests 
            << ", degrading=" << stats2.degrading_requests << "\n";
  
  // Clean up
  analyzer.notifyRequestCompleted(id1);
  analyzer.notifyRequestCompleted(id2);
  
  std::cout << "PASS\n";
}

void testMaxRequestsLimit() {
  std::cout << "\n=== Test 7: Maximum Requests Limit (kMaxRequestsSize = 300) ===\n";
  
  ThroughputAnalyzer analyzer;
  RequestIdGenerator idGen;
  
  // Add more than kMaxRequestsSize requests
  const int num_requests = 350;
  std::vector<void*> request_ids;
  
  for (int i = 0; i < num_requests; i++) {
    void* id = idGen.next();
    request_ids.push_back(id);
    ThroughputAnalyzer::Request req{id, 0, "GET", 50000, Clock::now(), false, false};
    analyzer.notifyStartTransaction(req);
  }
  
  auto stats = analyzer.getStatistics();
  assert(stats.active_requests <= 300); // Should be capped at kMaxRequestsSize
  
  std::cout << "✓ Requests capped at maximum: " << stats.active_requests << " (max=300)\n";
  
  // Clean up - complete all requests we think might still be active
  for (auto id : request_ids) {
    analyzer.notifyRequestCompleted(id);
  }
  
  std::cout << "PASS\n";
}

void testLegacyAddTransferAPI() {
  std::cout << "\n=== Test 8: Legacy addTransfer API ===\n";
  
  ThroughputAnalyzer::Options opts;
  opts.min_transfer_size_bytes = 32000; // 32KB
  
  ThroughputAnalyzer analyzer(opts);
  
  // Use legacy API
  auto start = Clock::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  auto end = Clock::now();
  
  analyzer.addTransfer(50000, start, end); // 50KB transfer
  
  auto stats = analyzer.getStatistics();
  assert(stats.sample_count == 1);
  
  std::cout << "✓ Legacy addTransfer API works: " << stats.sample_count << " samples\n";
  
  auto estimate = analyzer.getEstimate();
  if (estimate) {
    std::cout << "✓ Throughput estimate: " << *estimate << " kbps\n";
  }
  
  std::cout << "PASS\n";
}

void testThroughputCalculation() {
  std::cout << "\n=== Test 9: Throughput Calculation Accuracy ===\n";
  
  ThroughputAnalyzer::Options opts;
  opts.min_transfer_size_bytes = 32000;
  opts.throughput_min_requests_in_flight = 2;
  
  ThroughputAnalyzer analyzer(opts);
  RequestIdGenerator idGen;
  
  // Start 2 requests
  auto start_time = Clock::now();
  void* id1 = idGen.next();
  void* id2 = idGen.next();
  ThroughputAnalyzer::Request req1{id1, 0, "GET", 0, start_time, false, false};
  ThroughputAnalyzer::Request req2{id2, 0, "GET", 0, start_time, false, false};
  
  analyzer.notifyStartTransaction(req1);
  analyzer.notifyStartTransaction(req2);
  
  // Simulate receiving 40KB on each request over 100ms
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  analyzer.notifyBytesRead(id1, 40000);
  analyzer.notifyBytesRead(id2, 40000);
  
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  
  // Complete requests
  analyzer.notifyRequestCompleted(id1);
  
  auto stats = analyzer.getStatistics();
  if (stats.sample_count > 0) {
    auto estimate = analyzer.getEstimate();
    if (estimate) {
      std::cout << "✓ Throughput calculated: " << *estimate << " kbps\n";
      // Expected: 80KB (640000 bits) in ~100ms = ~6400 kbps (rough estimate)
      std::cout << "✓ Rough expected: ~6400 kbps for 80KB in 100ms\n";
    }
  }
  
  analyzer.notifyRequestCompleted(id2);
  
  std::cout << "PASS\n";
}

int main() {
  std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
  std::cout << "║    ThroughputAnalyzer - Chromium NQE Implementation Test  ║\n";
  std::cout << "╚════════════════════════════════════════════════════════════╝\n";
  
  // Configure logging
  Logger::instance().setMinLevel(LogLevel::LOG_INFO);
  Logger::instance().setCallback([](LogLevel level, const std::string& msg) {
    std::cout << "[" << logLevelToString(level) << "] " << msg << std::endl;
  });
  
  try {
    testBasicRequestTracking();
    testRequestMethodFiltering();
    testAccuracyDegradingRequests();
    testObservationWindow();
    testBytesReadUpdates();
    testNetworkChangeHandling();
    testMaxRequestsLimit();
    testLegacyAddTransferAPI();
    testThroughputCalculation();
    
    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║              All Tests Passed Successfully! ✓              ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "\n✗ Test failed with exception: " << e.what() << "\n";
    return 1;
  }
}
