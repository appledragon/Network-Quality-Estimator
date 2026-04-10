/**
 * Continuous HTTP monitoring with real websites
 * Demonstrates ECT recomputation with periodic real HTTP requests
 */

#include "nqe/Nqe.h"
#include "nqe/HttpRttSource.h"
#include "nqe/Logger.h"
#include "nqe/NetworkQualityObserver.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <atomic>
#include <iomanip>
#include <ctime>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#endif

#include <curl/curl.h>

using namespace std::chrono_literals;

// Callback to discard response body
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
  return size * nmemb;
}

// ECT Observer to track changes
class ECTMonitor : public nqe::EffectiveConnectionTypeObserver {
public:
  std::atomic<int> change_count{0};
  
  void onEffectiveConnectionTypeChanged(
      nqe::EffectiveConnectionType old_type,
      nqe::EffectiveConnectionType new_type) override {
    change_count++;
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    #ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
    #else
    localtime_r(&time_t, &tm_buf);
    #endif
    
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "  [" << std::put_time(&tm_buf, "%H:%M:%S") << "] ECT CHANGED!\n";
    std::cout << "  " << nqe::effectiveConnectionTypeToString(old_type) 
              << " -> " << nqe::effectiveConnectionTypeToString(new_type) << "\n";
    std::cout << "  Total changes: " << change_count << "\n";
    std::cout << std::string(60, '=') << "\n\n";
  }
};

// RTT Observer
class RTTMonitor : public nqe::RTTObserver {
public:
  void onRTTObservation(double rtt_ms, const char* source) override {
    // Optional: uncomment to see all RTT observations
    // std::cout << "  [RTT] " << source << ": " << rtt_ms << "ms\n";
  }
};

int main(int argc, char* argv[]) {
  // Setup logging
  nqe::Logger::instance().setMinLevel(nqe::LogLevel::LOG_INFO);
  nqe::Logger::instance().setCallback([](nqe::LogLevel level, const std::string& msg) {
    std::cout << "[" << nqe::logLevelToString(level) << "] " << msg << std::endl;
  });

  // Parse command line arguments
  int test_duration_seconds = 300; // Default 5 minutes
  int request_interval_ms = 3000;   // Default 3 seconds between requests
  int recomputation_interval_sec = 10; // Default 10 seconds ECT recomputation
  
  if (argc > 1) {
    test_duration_seconds = std::atoi(argv[1]);
  }
  if (argc > 2) {
    request_interval_ms = std::atoi(argv[2]);
  }
  if (argc > 3) {
    recomputation_interval_sec = std::atoi(argv[3]);
  }

  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "  Continuous HTTP Network Quality Monitor\n";
  std::cout << std::string(60, '=') << "\n";
  std::cout << "\nConfiguration:\n";
  std::cout << "  Duration: " << test_duration_seconds << " seconds\n";
  std::cout << "  Request interval: " << request_interval_ms << " ms\n";
  std::cout << "  ECT recomputation interval: " << recomputation_interval_sec << " seconds\n";
  std::cout << "\nUsage: " << argv[0] << " [duration_sec] [request_interval_ms] [recompute_interval_sec]\n\n";

  // Initialize libcurl
  curl_global_init(CURL_GLOBAL_DEFAULT);

  // Create NQE instance with custom options
  nqe::Nqe::Options opts;
  opts.decay_lambda_per_sec = 0.02;
  opts.effective_connection_type_recomputation_interval = std::chrono::seconds(recomputation_interval_sec);
  opts.count_new_observations_received_compute_ect = 10; // Trigger after 10 new observations
  
  std::string error;
  if (!nqe::Nqe::validateOptions(opts, &error)) {
    std::cerr << "Invalid options: " << error << std::endl;
    curl_global_cleanup();
    return 1;
  }

  nqe::Nqe estimator(opts);
  nqe::HttpRttSource http_source(estimator);

  // Register observers
  ECTMonitor ect_monitor;
  RTTMonitor rtt_monitor;
  estimator.addEffectiveConnectionTypeObserver(&ect_monitor);
  estimator.addRTTObserver(&rtt_monitor);

  // Rotating test websites (mix of fast and slow)
  std::vector<std::string> test_urls = {
    // Fast (usually)
    "https://www.google.com",
    "https://www.baidu.com",
    "https://www.cloudflare.com",
    "https://www.github.com",
    
    // Medium
    "https://www.bing.com",
    "https://www.amazon.com",
    "https://www.stackoverflow.com",
    
    // Sometimes slower
    "https://www.reddit.com",
    "https://www.wikipedia.org",
    "https://www.youtube.com",
  };

  std::cout << "Starting continuous monitoring...\n";
  std::cout << "Testing " << test_urls.size() << " websites in rotation\n";
  std::cout << "Press Ctrl+C to stop\n\n";

  auto start_time = std::chrono::steady_clock::now();
  auto end_time = start_time + std::chrono::seconds(test_duration_seconds);
  
  size_t request_count = 0;
  size_t success_count = 0;
  size_t url_index = 0;

  while (std::chrono::steady_clock::now() < end_time) {
    const std::string& url = test_urls[url_index % test_urls.size()];
    uint64_t request_id = request_count++;
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    #ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
    #else
    localtime_r(&time_t, &tm_buf);
    #endif
    
    std::cout << "[" << std::put_time(&tm_buf, "%H:%M:%S") << "] ";
    std::cout << "Request #" << request_id << ": " << url << std::flush;

    // Create CURL handle
    CURL* curl = curl_easy_init();
    if (!curl) {
      std::cout << " X CURL init failed\n";
      continue;
    }

    // Configure request
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD request
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    // Notify HTTP source about request start
    auto request_start = nqe::Clock::now();
    http_source.onRequestSent(request_id, request_start);

    // Perform request
    CURLcode res = curl_easy_perform(curl);

    auto request_end = nqe::Clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        request_end - request_start).count();

    if (res == CURLE_OK) {
      success_count++;
      
      // Notify response headers received
      http_source.onResponseHeaders(request_id, request_end);
      
      // Get current estimate and ECT
      auto estimate = estimator.getEstimate();
      auto ect = estimator.getEffectiveConnectionType();
      
      std::cout << " OK " << duration << "ms";
      std::cout << " | NQE: " << std::fixed << std::setprecision(1) << estimate.rtt_ms << "ms";
      std::cout << " | ECT: " << nqe::effectiveConnectionTypeToString(ect) << "\n";
    } else {
      std::cout << " X " << curl_easy_strerror(res) << "\n";
    }

    curl_easy_cleanup(curl);
    
    // Move to next URL
    url_index++;
    
    // Wait before next request
    std::this_thread::sleep_for(std::chrono::milliseconds(request_interval_ms));
  }

  // Print final statistics
  std::cout << "\n" << std::string(60, '=') << "\n";
  std::cout << "  Monitoring Complete\n";
  std::cout << std::string(60, '=') << "\n\n";
  
  std::cout << "Statistics:\n";
  std::cout << "  Total requests: " << request_count << "\n";
  size_t divisor = (request_count > 0) ? request_count : 1;
  std::cout << "  Successful: " << success_count << " (" 
            << (success_count * 100 / divisor) << "%)\n";
  std::cout << "  ECT changes: " << ect_monitor.change_count << "\n\n";
  
  auto final_estimate = estimator.getEstimate();
  auto final_ect = estimator.getEffectiveConnectionType();
  
  std::cout << "Final Network Quality:\n";
  std::cout << "  RTT: " << final_estimate.rtt_ms << "ms\n";
  if (final_estimate.http_ttfb_ms) {
    std::cout << "  HTTP TTFB: " << *final_estimate.http_ttfb_ms << "ms\n";
  }
  std::cout << "  ECT: " << nqe::effectiveConnectionTypeToString(final_ect) << "\n";

  // Cleanup
  estimator.removeEffectiveConnectionTypeObserver(&ect_monitor);
  estimator.removeRTTObserver(&rtt_monitor);
  curl_global_cleanup();

  return 0;
}
