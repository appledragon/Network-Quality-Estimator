/**
 * Advanced Features Demo
 * 
 * Demonstrates the new Chrome NQE-inspired features:
 * - End-to-End RTT observation category
 * - HTTP RTT bounding logic
 * - HTTP RTT adjustment based on sample counts
 * - Throughput clamping based on ECT
 * - Granular observation sources
 */

#include "nqe/Nqe.h"
#include "nqe/Logger.h"
#include "nqe/NetworkQualityObserver.h"

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <curl/curl.h>

using namespace std::chrono_literals;

// Helper for real HTTP requests
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
  return size * nmemb;
}

static size_t write_callback_count(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total = size * nmemb;
  size_t* counter = static_cast<size_t*>(userp);
  *counter += total;
  return total;
}

struct RealRequestResult {
  bool success;
  double ttfb_ms;
  size_t bytes;
};

RealRequestResult performRealGetRequest(const std::string& url) {
  RealRequestResult result{false, 0.0, 0};
  CURL* curl = curl_easy_init();
  if (!curl) return result;
  
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_count);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.bytes);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  
  CURLcode res = curl_easy_perform(curl);
  if (res == CURLE_OK) {
    result.success = true;
    double ttfb = 0;
    curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &ttfb);
    result.ttfb_ms = ttfb * 1000.0;
  }
  
  curl_easy_cleanup(curl);
  return result;
}

class DetailedObserver : public nqe::RTTObserver, public nqe::EffectiveConnectionTypeObserver {
public:
  void onRTTObservation(double rtt_ms, const char* source) override {
    std::cout << "  [RTT Sample] " << std::setw(15) << std::left << source 
              << ": " << std::fixed << std::setprecision(1) << rtt_ms << " ms\n";
  }
  
  void onEffectiveConnectionTypeChanged(
      nqe::EffectiveConnectionType old_type,
      nqe::EffectiveConnectionType new_type) override {
    std::cout << "\n*** ECT CHANGED: " 
              << nqe::effectiveConnectionTypeToString(old_type)
              << " -> " << nqe::effectiveConnectionTypeToString(new_type) 
              << " ***\n\n";
  }
};

void printEstimate(const nqe::Estimate& e) {
  std::cout << "\n=== Current Estimate ===\n";
  std::cout << "Combined RTT:     " << std::fixed << std::setprecision(1) << e.rtt_ms << " ms\n";
  
  if (e.http_ttfb_ms) {
    std::cout << "  HTTP RTT:       " << *e.http_ttfb_ms << " ms\n";
  }
  if (e.transport_rtt_ms) {
    std::cout << "  Transport RTT:  " << *e.transport_rtt_ms << " ms\n";
  }
  if (e.ping_rtt_ms) {
    std::cout << "  Ping RTT:       " << *e.ping_rtt_ms << " ms\n";
  }
  if (e.end_to_end_rtt_ms) {
    std::cout << "  End-to-End RTT: " << *e.end_to_end_rtt_ms << " ms\n";
  }
  if (e.throughput_kbps) {
    std::cout << "  Throughput:     " << *e.throughput_kbps << " kbps\n";
  }
  
  std::cout << "ECT:              " << nqe::effectiveConnectionTypeToString(e.effective_type) << "\n";
  std::cout << "========================\n\n";
}

void printStatistics(const nqe::Statistics& stats) {
  std::cout << "\n=== Detailed Statistics ===\n";
  
  auto printSource = [](const char* name, const nqe::SourceStatistics& s) {
    std::cout << name << ":\n";
    std::cout << "  Samples: " << s.sample_count << "\n";
    if (s.min_ms) std::cout << "  Min:     " << *s.min_ms << " ms\n";
    if (s.max_ms) std::cout << "  Max:     " << *s.max_ms << " ms\n";
    if (s.percentile_50th) std::cout << "  P50:     " << *s.percentile_50th << " ms\n";
    if (s.percentile_95th) std::cout << "  P95:     " << *s.percentile_95th << " ms\n";
  };
  
  printSource("HTTP", stats.http);
  printSource("Transport", stats.transport);
  printSource("Ping", stats.ping);
  printSource("End-to-End", stats.end_to_end);
  
  std::cout << "\nThroughput:\n";
  std::cout << "  Samples: " << stats.throughput.sample_count << "\n";
  if (stats.throughput.percentile_50th) {
    std::cout << "  P50:     " << *stats.throughput.percentile_50th << " kbps\n";
  }
  
  std::cout << "\nTotal samples: " << stats.total_samples << "\n";
  std::cout << "==========================\n\n";
}

int main() {
  // Initialize CURL
  curl_global_init(CURL_GLOBAL_DEFAULT);
  
  // Setup logging
  nqe::Logger::instance().setMinLevel(nqe::LogLevel::LOG_INFO);
  nqe::Logger::instance().setCallback([](nqe::LogLevel level, const std::string& msg) {
    std::cout << "[" << nqe::logLevelToString(level) << "] " << msg << std::endl;
  });

  std::cout << "\n" << std::string(70, '=') << "\n";
  std::cout << "  Advanced NQE Features Demo\n";
  std::cout << "  Chrome NQE-Inspired Enhancements\n";
  std::cout << "  (Including Real Website Testing)\n";
  std::cout << std::string(70, '=') << "\n\n";

  // Create NQE with enhanced options
  nqe::Nqe::Options opts;
  opts.decay_lambda_per_sec = 0.02;
  opts.effective_connection_type_recomputation_interval = std::chrono::seconds(5);
  opts.count_new_observations_received_compute_ect = 10;
  
  // Enable HTTP RTT bounding
  opts.lower_bound_http_rtt_transport_rtt_multiplier = 1.0;
  opts.lower_bound_http_rtt_end_to_end_rtt_multiplier = 0.9;
  opts.upper_bound_http_rtt_end_to_end_rtt_multiplier = 1.6;
  opts.http_rtt_transport_rtt_min_count = 3;
  opts.http_rtt_end_to_end_rtt_min_count = 3;
  opts.adjust_rtt_based_on_rtt_counts = true;
  
  // Enable throughput clamping
  opts.clamp_throughput_based_on_ect = true;
  opts.upper_bound_typical_kbps_multiplier = 3.5;
  
  nqe::Nqe estimator(opts);
  
  DetailedObserver observer;
  estimator.addRTTObserver(&observer);
  estimator.addEffectiveConnectionTypeObserver(&observer);

  std::cout << "Feature Demonstration Scenarios:\n\n";

  // ========================================================================
  // Scenario 1: HTTP RTT Bounding by Transport RTT
  // ========================================================================
  std::cout << std::string(70, '-') << "\n";
  std::cout << "Scenario 1: HTTP RTT Bounding by Transport RTT\n";
  std::cout << std::string(70, '-') << "\n";
  std::cout << "Adding transport RTT samples first, then HTTP RTT.\n";
  std::cout << "HTTP RTT should be bounded to >= Transport RTT.\n\n";

  auto now = nqe::Clock::now();
  
  // Add transport RTT samples (lower values - fast connection)
  for (int i = 0; i < 5; i++) {
    estimator.addSample(nqe::Source::TCP, 50.0 + i * 2, now);
    now += 100ms;
  }
  
  // Add HTTP RTT sample that's artificially low (will be bounded upward)
  std::cout << "Adding HTTP RTT = 40ms (lower than transport RTT ~52ms)\n";
  estimator.addSample(nqe::Source::HTTP_TTFB, 40.0, now);
  now += 100ms;
  
  printEstimate(estimator.getEstimate());
  
  std::this_thread::sleep_for(1s);

  // ========================================================================
  // Scenario 2: End-to-End RTT Bounding
  // ========================================================================
  std::cout << std::string(70, '-') << "\n";
  std::cout << "Scenario 2: End-to-End RTT Bounding\n";
  std::cout << std::string(70, '-') << "\n";
  std::cout << "Adding H2 PING (end-to-end) RTT samples.\n";
  std::cout << "HTTP RTT should be bounded by end-to-end RTT.\n\n";

  // Add end-to-end RTT samples from H2 PINGs
  for (int i = 0; i < 5; i++) {
    estimator.addSample(nqe::Source::H2_PINGS, 100.0 + i * 5, now);
    now += 200ms;
  }
  
  // Add HTTP RTT that's too high (will be bounded by upper_multiplier)
  std::cout << "Adding HTTP RTT = 200ms\n";
  std::cout << "Expected bounding by end-to-end RTT (~110ms × 1.6 = 176ms max)\n";
  estimator.addSample(nqe::Source::HTTP_TTFB, 200.0, now);
  now += 100ms;
  
  printEstimate(estimator.getEstimate());
  
  std::this_thread::sleep_for(1s);

  // ========================================================================
  // Scenario 3: HTTP RTT Adjustment with Low Sample Count
  // ========================================================================
  std::cout << std::string(70, '-') << "\n";
  std::cout << "Scenario 3: HTTP RTT Adjustment with Low Transport Samples\n";
  std::cout << std::string(70, '-') << "\n";
  std::cout << "Simulating scenario with few transport RTT samples.\n";
  std::cout << "HTTP RTT may be adjusted to typical value for the ECT.\n\n";

  // Clear and restart
  nqe::Nqe estimator2(opts);
  estimator2.addRTTObserver(&observer);
  estimator2.addEffectiveConnectionTypeObserver(&observer);
  
  now = nqe::Clock::now();
  
  // Add only 2 transport RTT samples (below threshold of 3)
  estimator2.addSample(nqe::Source::TCP, 300.0, now);
  now += 100ms;
  estimator2.addSample(nqe::Source::TCP, 320.0, now);
  now += 100ms;
  
  // Add very high HTTP RTT (simulating hanging request)
  std::cout << "Adding HTTP RTT = 1500ms with only 2 transport samples\n";
  std::cout << "Expected adjustment to typical 3G HTTP RTT threshold (400ms)\n";
  estimator2.addSample(nqe::Source::HTTP_TTFB, 1500.0, now);
  now += 100ms;
  
  printEstimate(estimator2.getEstimate());
  
  std::this_thread::sleep_for(1s);

  // ========================================================================
  // Scenario 4: Throughput Clamping on Slow Connection
  // ========================================================================
  std::cout << std::string(70, '-') << "\n";
  std::cout << "Scenario 4: Throughput Clamping Based on ECT\n";
  std::cout << std::string(70, '-') << "\n";
  std::cout << "Simulating slow 2G connection with anomalous high throughput.\n";
  std::cout << "Throughput should be clamped to typical_kbps × multiplier.\n\n";

  nqe::Nqe estimator3(opts);
  estimator3.addRTTObserver(&observer);
  estimator3.addEffectiveConnectionTypeObserver(&observer);
  
  now = nqe::Clock::now();
  
  // Add high RTT samples to establish Slow-2G classification
  for (int i = 0; i < 8; i++) {
    estimator3.addSample(nqe::Source::HTTP_TTFB, 2100.0 + i * 50, now);
    now += 200ms;
  }
  
  // Add anomalously high throughput sample
  // For Slow-2G (typical 50 kbps), max should be 50 × 3.5 = 175 kbps
  std::cout << "Adding throughput = 500 kbps on Slow-2G connection\n";
  std::cout << "Expected clamping to ~175 kbps (50 kbps × 3.5)\n";
  
  auto start = now;
  auto end = now + 1s;
  size_t bytes = (500 * 1000 / 8); // 500 kbps for 1 second
  estimator3.addThroughputSample(bytes, start, end);
  now = end + 100ms;
  
  printEstimate(estimator3.getEstimate());
  
  // ========================================================================
  // Scenario 5: Granular Source Types
  // ========================================================================
  std::cout << std::string(70, '-') << "\n";
  std::cout << "Scenario 5: Granular Observation Sources\n";
  std::cout << std::string(70, '-') << "\n";
  std::cout << "Demonstrating different observation source types:\n";
  std::cout << "TCP, QUIC, H2_PINGS, H3_PINGS, CACHED_ESTIMATE, etc.\n\n";

  nqe::Nqe estimator4(opts);
  estimator4.addRTTObserver(&observer);
  
  now = nqe::Clock::now();
  
  std::cout << "Adding samples from various sources:\n";
  estimator4.addSample(nqe::Source::TCP, 80.0, now);
  now += 50ms;
  
  estimator4.addSample(nqe::Source::QUIC, 75.0, now);
  now += 50ms;
  
  estimator4.addSample(nqe::Source::H2_PINGS, 90.0, now);
  now += 50ms;
  
  estimator4.addSample(nqe::Source::H3_PINGS, 85.0, now);
  now += 50ms;
  
  estimator4.addSample(nqe::Source::HTTP_CACHED_ESTIMATE, 120.0, now);
  now += 50ms;
  
  estimator4.addSample(nqe::Source::TRANSPORT_CACHED_ESTIMATE, 70.0, now);
  now += 50ms;
  
  std::cout << "\n";
  printStatistics(estimator4.getStatistics());

  // ========================================================================
  // Summary
  // ========================================================================
  std::cout << std::string(70, '=') << "\n";
  std::cout << "  Demo Complete - New Features Demonstrated:\n";
  std::cout << std::string(70, '=') << "\n";
  std::cout << "✓ End-to-End RTT observation category (H2/H3 PING)\n";
  std::cout << "✓ HTTP RTT bounding by Transport RTT\n";
  std::cout << "✓ HTTP RTT bounding by End-to-End RTT (lower and upper)\n";
  std::cout << "✓ HTTP RTT adjustment when sample count is low\n";
  std::cout << "✓ Throughput clamping based on ECT\n";
  std::cout << "✓ Granular observation sources (TCP, QUIC, H2_PINGS, etc.)\n";
  std::cout << std::string(70, '=') << "\n\n";

  curl_global_cleanup();
  return 0;
}
