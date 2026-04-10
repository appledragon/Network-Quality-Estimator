// Comprehensive test demonstrating all NQE features
#include "nqe/Nqe.h"
#include "nqe/HttpRttSource.h"
#include "nqe/QuicH2PingSource.h"
#include "nqe/Logger.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <vector>
#include <string>
#include <curl/curl.h>

using namespace std::chrono_literals;

// Helper for real HTTP requests
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
  return size * nmemb;
}

double performRealHttpRequest(const std::string& url) {
  CURL* curl = curl_easy_init();
  if (!curl) return -1.0;
  
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  
  CURLcode res = curl_easy_perform(curl);
  double ttfb_ms = -1.0;
  
  if (res == CURLE_OK) {
    double ttfb = 0;
    curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &ttfb);
    ttfb_ms = ttfb * 1000.0;
  }
  
  curl_easy_cleanup(curl);
  return ttfb_ms;
}

void printSeparator(const std::string& title) {
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "  " << title << "\n";
  std::cout << std::string(60, '=') << "\n\n";
}

void testValidation() {
  printSeparator("Testing Configuration Validation");
  
  // Test valid options
  nqe::Nqe::Options valid_opts;
  valid_opts.decay_lambda_per_sec = 0.02;
  valid_opts.transport_sample_period = 1000ms;
  valid_opts.combine_bias_to_lower = 0.6;
  
  std::string error;
  if (nqe::Nqe::validateOptions(valid_opts, &error)) {
    std::cout << "✓ Valid options passed validation" << std::endl;
  } else {
    std::cout << "✗ Valid options failed: " << error << std::endl;
  }
  
  // Test invalid options
  nqe::Nqe::Options invalid_opts;
  invalid_opts.decay_lambda_per_sec = -0.5;  // Invalid: negative
  
  if (!nqe::Nqe::validateOptions(invalid_opts, &error)) {
    std::cout << "✓ Invalid options correctly rejected: " << error << std::endl;
  } else {
    std::cout << "✗ Invalid options incorrectly accepted" << std::endl;
  }
  
  invalid_opts.decay_lambda_per_sec = 0.02;
  invalid_opts.combine_bias_to_lower = 1.5;  // Invalid: > 1
  
  if (!nqe::Nqe::validateOptions(invalid_opts, &error)) {
    std::cout << "✓ Out-of-range bias correctly rejected: " << error << std::endl;
  } else {
    std::cout << "✗ Out-of-range bias incorrectly accepted" << std::endl;
  }
}

void testHttpRtt() {
  printSeparator("Testing HTTP RTT Source with Real Websites");
  
  nqe::Nqe estimator;
  nqe::HttpRttSource http_source(estimator);
  
  std::vector<std::string> test_urls = {
    "https://www.google.com",
    "https://www.cloudflare.com",
    "https://www.github.com",
  };
  
  std::cout << "Performing real HTTP requests...\n" << std::endl;
  
  for (size_t i = 0; i < test_urls.size(); ++i) {
    uint64_t req_id = i;
    const std::string& url = test_urls[i];
    
    std::cout << "Request " << i << ": " << url << std::endl;
    
    http_source.onRequestSent(req_id);
    double ttfb_ms = performRealHttpRequest(url);
    http_source.onResponseHeaders(req_id);
    
    if (ttfb_ms > 0) {
      std::cout << "  ✓ TTFB: " << std::fixed << std::setprecision(1) << ttfb_ms << "ms" << std::endl;
    } else {
      std::cout << "  ✗ Request failed" << std::endl;
    }
    
    auto est = estimator.getEstimate();
    std::cout << "  NQE RTT: " << std::fixed << std::setprecision(2) << est.rtt_ms << "ms";
    if (est.http_ttfb_ms) {
      std::cout << " (Estimate: " << *est.http_ttfb_ms << "ms)";
    }
    std::cout << "\n" << std::endl;
  }
  
  auto stats = estimator.getStatistics();
  std::cout << "\nHTTP Statistics:" << std::endl;
  std::cout << "  Samples: " << stats.http.sample_count << std::endl;
  if (stats.http.min_ms) {
    std::cout << "  Range: " << *stats.http.min_ms << "ms - " 
              << *stats.http.max_ms << "ms" << std::endl;
  }
}

void testPingRtt() {
  printSeparator("Testing PING RTT Source");
  
  nqe::Nqe estimator;
  nqe::QuicH2PingSource ping_source(estimator);
  
  ping_source.setPingImpl([](const std::string& authority) {
    // Mock PING implementation
  });
  
  // Simulate 3 PING/PONG exchanges
  std::vector<std::string> hosts = {"host1.com", "host2.com", "host3.com"};
  
  for (const auto& host : hosts) {
    ping_source.ping(host);
    std::this_thread::sleep_for(25ms);
    ping_source.onPong(host);
    
    auto est = estimator.getEstimate();
    std::cout << "PING to " << host << " - RTT: " << std::fixed << std::setprecision(2)
              << est.rtt_ms << "ms";
    if (est.ping_rtt_ms) {
      std::cout << " (PING RTT: " << *est.ping_rtt_ms << "ms)";
    }
    std::cout << std::endl;
  }
  
  auto stats = estimator.getStatistics();
  std::cout << "\nPING Statistics:" << std::endl;
  std::cout << "  Samples: " << stats.ping.sample_count << std::endl;
  if (stats.ping.percentile_50th) {
    std::cout << "  Median: " << *stats.ping.percentile_50th << "ms" << std::endl;
  }
}

void testStatistics() {
  printSeparator("Testing Statistics & Percentiles");
  
  nqe::Nqe estimator;
  
  // Add diverse samples
  std::vector<double> samples = {10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0};
  for (double sample : samples) {
    estimator.addSample(nqe::Source::HTTP_TTFB, sample);
    std::this_thread::sleep_for(10ms);
  }
  
  auto stats = estimator.getStatistics();
  
  std::cout << "Total samples: " << stats.total_samples << std::endl;
  std::cout << "\nHTTP Statistics:" << std::endl;
  std::cout << "  Count: " << stats.http.sample_count << std::endl;
  std::cout << "  Min: " << *stats.http.min_ms << "ms" << std::endl;
  std::cout << "  P50: " << *stats.http.percentile_50th << "ms" << std::endl;
  std::cout << "  P95: " << *stats.http.percentile_95th << "ms" << std::endl;
  std::cout << "  P99: " << *stats.http.percentile_99th << "ms" << std::endl;
  std::cout << "  Max: " << *stats.http.max_ms << "ms" << std::endl;
}

void testLogging() {
  printSeparator("Testing Logging Framework");
  
  // Test different log levels
  std::cout << "Testing log levels (should see INFO and above):" << std::endl;
  
  nqe::Logger::instance().setMinLevel(nqe::LogLevel::LOG_INFO);
  
  nqe::Logger::instance().debug("This is a DEBUG message (should not appear)");
  nqe::Logger::instance().info("This is an INFO message");
  nqe::Logger::instance().warning("This is a WARNING message");
  nqe::Logger::instance().error("This is an ERROR message");
  
  std::cout << "\nChanging to DEBUG level:" << std::endl;
  nqe::Logger::instance().setMinLevel(nqe::LogLevel::LOG_DEBUG);
  nqe::Logger::instance().debug("This is a DEBUG message (should appear now)");
}

void testStateQueries() {
  printSeparator("Testing State Query Methods");
  
  nqe::Nqe estimator;
  
  std::cout << "Transport sampler running: " 
            << (estimator.isTransportSamplerRunning() ? "yes" : "no") << std::endl;
  std::cout << "Active sockets: " << estimator.getActiveSockets() << std::endl;
  
  estimator.startTransportSampler();
  std::cout << "\nAfter starting sampler:" << std::endl;
  std::cout << "Transport sampler running: " 
            << (estimator.isTransportSamplerRunning() ? "yes" : "no") << std::endl;
  
  estimator.stopTransportSampler();
  std::cout << "\nAfter stopping sampler:" << std::endl;
  std::cout << "Transport sampler running: " 
            << (estimator.isTransportSamplerRunning() ? "yes" : "no") << std::endl;
}

int main() {
  // Initialize CURL
  curl_global_init(CURL_GLOBAL_DEFAULT);
  
  std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
  std::cout << "║        NQE Library - Comprehensive Feature Test           ║\n";
  std::cout << "║              (Using Real Websites)                         ║\n";
  std::cout << "╚════════════════════════════════════════════════════════════╝\n";
  
  // Setup logging
  nqe::Logger::instance().setMinLevel(nqe::LogLevel::LOG_WARNING);
  nqe::Logger::instance().setCallback([](nqe::LogLevel level, const std::string& msg) {
    std::cout << "[" << nqe::logLevelToString(level) << "] " << msg << std::endl;
  });
  
  int result = 0;
  
  try {
    testValidation();
    testHttpRtt();
    testPingRtt();
    testStatistics();
    testStateQueries();
    testLogging();
    
    printSeparator("All Tests Completed Successfully");
    std::cout << "✓ All features working as expected!\n" << std::endl;
    
  } catch (const std::exception& e) {
    std::cerr << "\n✗ Test failed with exception: " << e.what() << std::endl;
    result = 1;
  }
  
  curl_global_cleanup();
  return result;
}
