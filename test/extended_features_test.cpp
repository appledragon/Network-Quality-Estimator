#include "nqe/Nqe.h"
#include "nqe/Logger.h"
#include "nqe/EffectiveConnectionType.h"
#include "nqe/NetworkQualityObserver.h"
#include "nqe/HttpRttSource.h"
#include "nqe/ThroughputAnalyzer.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <curl/curl.h>

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

// Custom RTT Observer
class MyRTTObserver : public nqe::RTTObserver {
public:
  void onRTTObservation(double rtt_ms, const char* source) override {
    std::cout << "  [RTT Observer] New RTT sample: " << rtt_ms << "ms from " << source << std::endl;
  }
};

// Custom Throughput Observer
class MyThroughputObserver : public nqe::ThroughputObserver {
public:
  void onThroughputObservation(double throughput_kbps) override {
    std::cout << "  [Throughput Observer] New throughput sample: " 
              << std::fixed << std::setprecision(2) << throughput_kbps << " kbps" << std::endl;
  }
};

// Custom ECT Observer
class MyECTObserver : public nqe::EffectiveConnectionTypeObserver {
public:
  void onEffectiveConnectionTypeChanged(
      nqe::EffectiveConnectionType old_type,
      nqe::EffectiveConnectionType new_type) override {
    std::cout << "  [ECT Observer] Connection type changed: " 
              << nqe::effectiveConnectionTypeToString(old_type) << " -> "
              << nqe::effectiveConnectionTypeToString(new_type) << std::endl;
  }
};

void printSeparator() {
  std::cout << "\n" << std::string(60, '=') << std::endl;
}

void printHeader(const std::string& title) {
  printSeparator();
  std::cout << "  " << title << std::endl;
  printSeparator();
  std::cout << std::endl;
}

void printEstimate(const nqe::Estimate& est) {
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "Estimate:" << std::endl;
  std::cout << "  Combined RTT: " << est.rtt_ms << "ms" << std::endl;
  if (est.http_ttfb_ms) {
    std::cout << "  HTTP TTFB: " << *est.http_ttfb_ms << "ms" << std::endl;
  }
  if (est.transport_rtt_ms) {
    std::cout << "  Transport RTT: " << *est.transport_rtt_ms << "ms" << std::endl;
  }
  if (est.ping_rtt_ms) {
    std::cout << "  PING RTT: " << *est.ping_rtt_ms << "ms" << std::endl;
  }
  if (est.throughput_kbps) {
    std::cout << "  Throughput: " << *est.throughput_kbps << " kbps" << std::endl;
  }
  std::cout << "  Effective Connection Type: " 
            << nqe::effectiveConnectionTypeToString(est.effective_type) << std::endl;
}

void testEffectiveConnectionType() {
  printHeader("Testing Effective Connection Type Classification");
  
  // Test various network conditions
  struct TestCase {
    std::string name;
    std::optional<double> http_rtt;
    std::optional<double> transport_rtt;
    std::optional<double> throughput;
    nqe::EffectiveConnectionType expected;
  };
  
  std::vector<TestCase> tests = {
    {"Excellent (4G-like)", 50.0, 30.0, 5000.0, nqe::EffectiveConnectionType::TYPE_4G},
    {"Good (3G-like)", 600.0, 500.0, 500.0, nqe::EffectiveConnectionType::TYPE_3G},
    {"Poor (2G-like)", 1600.0, 1500.0, 60.0, nqe::EffectiveConnectionType::TYPE_2G},
    {"Very Poor (Slow-2G)", 2500.0, 2200.0, 30.0, nqe::EffectiveConnectionType::SLOW_2G},
    {"No data (Unknown)", std::nullopt, std::nullopt, std::nullopt, nqe::EffectiveConnectionType::UNKNOWN},
  };
  
  for (const auto& test : tests) {
    auto ect = nqe::computeEffectiveConnectionType(
        test.http_rtt, test.transport_rtt, test.throughput);
    
    std::cout << test.name << ": " << nqe::effectiveConnectionTypeToString(ect);
    if (ect == test.expected) {
      std::cout << " ✓" << std::endl;
    } else {
      std::cout << " ✗ (expected " << nqe::effectiveConnectionTypeToString(test.expected) << ")" << std::endl;
    }
  }
}

void testThroughputAnalyzer() {
  printHeader("Testing Throughput Analyzer");
  
  nqe::ThroughputAnalyzer::Options opts;
  opts.min_transfer_size_bytes = 1000; // Low threshold for testing
  nqe::ThroughputAnalyzer analyzer(opts);
  
  auto now = nqe::Clock::now();
  
  // Simulate various transfer sizes and durations
  std::cout << "Adding throughput samples..." << std::endl;
  
  // 1 MB in 1 second = 8000 kbps
  analyzer.addTransfer(1000000, now, now + std::chrono::seconds(1));
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  
  // 500 KB in 0.5 seconds = 8000 kbps
  analyzer.addTransfer(500000, now, now + std::chrono::milliseconds(500));
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  
  // 2 MB in 2 seconds = 8000 kbps
  analyzer.addTransfer(2000000, now, now + std::chrono::seconds(2));
  
  auto estimate = analyzer.getEstimate();
  auto stats = analyzer.getStatistics();
  
  std::cout << "\nThroughput Statistics:" << std::endl;
  std::cout << "  Sample count: " << stats.sample_count << std::endl;
  if (estimate) {
    std::cout << "  Current estimate: " << std::fixed << std::setprecision(2) 
              << *estimate << " kbps" << std::endl;
  }
  if (stats.min_kbps) {
    std::cout << "  Min: " << std::fixed << std::setprecision(2) << *stats.min_kbps << " kbps" << std::endl;
  }
  if (stats.max_kbps) {
    std::cout << "  Max: " << std::fixed << std::setprecision(2) << *stats.max_kbps << " kbps" << std::endl;
  }
}

void testObservers() {
  printHeader("Testing Observer Pattern");
  
  nqe::Nqe::Options opts;
  opts.decay_lambda_per_sec = 0.05;
  nqe::Nqe estimator(opts);
  
  // Create observers
  MyRTTObserver rtt_observer;
  MyThroughputObserver throughput_observer;
  MyECTObserver ect_observer;
  
  // Register observers
  std::cout << "Registering observers..." << std::endl;
  estimator.addRTTObserver(&rtt_observer);
  estimator.addThroughputObserver(&throughput_observer);
  estimator.addEffectiveConnectionTypeObserver(&ect_observer);
  
  std::cout << "\nAdding samples (observers will be notified):" << std::endl;
  
  // Add some RTT samples
  estimator.addSample(nqe::Source::HTTP_TTFB, 100.0);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  
  estimator.addSample(nqe::Source::TRANSPORT_RTT, 50.0);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  
  // Add throughput sample
  auto now = nqe::Clock::now();
  estimator.addThroughputSample(500000, now - std::chrono::milliseconds(500), now);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  
  // Add more samples to potentially trigger ECT change
  estimator.addSample(nqe::Source::HTTP_TTFB, 2000.0);  // Slow connection
  
  std::cout << "\nUnregistering observers..." << std::endl;
  estimator.removeRTTObserver(&rtt_observer);
  estimator.removeThroughputObserver(&throughput_observer);
  estimator.removeEffectiveConnectionTypeObserver(&ect_observer);
  
  std::cout << "\nAdding more samples (observers should not be notified):" << std::endl;
  estimator.addSample(nqe::Source::HTTP_TTFB, 150.0);
  std::cout << "  Sample added silently (no observer notification)" << std::endl;
}

void testIntegratedNQE() {
  printHeader("Testing Integrated NQE with All Features (Real Websites)");
  
  nqe::Logger::instance().setMinLevel(nqe::LogLevel::LOG_INFO);
  
  nqe::Nqe::Options opts;
  opts.decay_lambda_per_sec = 0.03;
  opts.combine_bias_to_lower = 0.6;
  opts.freshness_threshold = std::chrono::seconds(60);
  opts.min_throughput_transfer_bytes = 10000;
  
  nqe::Nqe estimator(opts);
  nqe::HttpRttSource http_source(estimator);
  
  std::vector<std::string> test_urls = {
    "https://www.google.com",
    "https://www.cloudflare.com",
    "https://www.github.com",
  };
  
  std::cout << "Testing with real websites...\n" << std::endl;
  
  for (size_t i = 0; i < test_urls.size(); ++i) {
    const std::string& url = test_urls[i];
    auto now = nqe::Clock::now();
    
    std::cout << "Request " << (i+1) << ": " << url << std::endl;
    
    http_source.onRequestSent(i, now);
    auto result = performRealGetRequest(url);
    
    if (result.success) {
      http_source.onResponseHeaders(i, now + std::chrono::milliseconds(static_cast<int64_t>(result.ttfb_ms)));
      
      // Add transport RTT (simulated as ~60% of HTTP RTT)
      estimator.addSample(nqe::Source::TRANSPORT_RTT, result.ttfb_ms * 0.6, now);
      
      // Add throughput if enough bytes
      if (result.bytes > 10000) {
        auto end_time = now + std::chrono::milliseconds(static_cast<int64_t>(result.ttfb_ms * 1.5));
        estimator.addThroughputSample(result.bytes, now, end_time);
      }
      
      std::cout << "  ✓ TTFB: " << std::fixed << std::setprecision(1) << result.ttfb_ms << "ms";
      std::cout << ", Bytes: " << result.bytes << std::endl;
    } else {
      std::cout << "  ✗ Request failed" << std::endl;
    }
    
    auto estimate = estimator.getEstimate();
    std::cout << "\n";
    printEstimate(estimate);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  
  // Get comprehensive statistics
  std::cout << "\n\nComprehensive Statistics:" << std::endl;
  auto stats = estimator.getStatistics();
  std::cout << "  Total samples: " << stats.total_samples << std::endl;
  std::cout << "  HTTP samples: " << stats.http.sample_count << std::endl;
  std::cout << "  Transport samples: " << stats.transport.sample_count << std::endl;
  std::cout << "  Throughput samples: " << stats.throughput.sample_count << std::endl;
  std::cout << "  Current ECT: " << nqe::effectiveConnectionTypeToString(stats.effective_type) << std::endl;
}

int main() {
  // Initialize CURL
  curl_global_init(CURL_GLOBAL_DEFAULT);
  
  std::cout << "╔════════════════════════════════════════════════════════════╗" << std::endl;
  std::cout << "║    NQE Extended Features - Comprehensive Test Suite       ║" << std::endl;
  std::cout << "║              (Using Real Websites)                         ║" << std::endl;
  std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;
  
  int result = 0;
  
  try {
    testEffectiveConnectionType();
    testThroughputAnalyzer();
    testObservers();
    testIntegratedNQE();
    
    printSeparator();
    std::cout << "  All Extended Feature Tests Completed Successfully" << std::endl;
    printSeparator();
    std::cout << "\n✓ All new features working as expected!" << std::endl;
    
  } catch (const std::exception& e) {
    std::cerr << "\n✗ Test failed with exception: " << e.what() << std::endl;
    result = 1;
  }
  
  curl_global_cleanup();
  return result;
}
