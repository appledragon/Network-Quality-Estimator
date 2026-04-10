// Example demonstrating HTTP RTT measurement with real websites using libcurl
#include "nqe/Nqe.h"
#include "nqe/HttpRttSource.h"
#include "nqe/Logger.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#endif

#include <curl/curl.h>

using namespace std::chrono_literals;

// Callback to discard response body (we only care about timing)
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
  return size * nmemb;
}

struct RequestInfo {
  uint64_t id;
  std::string url;
  nqe::HttpRttSource* http_source;
  nqe::Clock::time_point start_time;
};

int main() {
  // Setup logging
  nqe::Logger::instance().setMinLevel(nqe::LogLevel::LOG_INFO);
  nqe::Logger::instance().setCallback([](nqe::LogLevel level, const std::string& msg) {
    std::cout << "[" << nqe::logLevelToString(level) << "] " << msg << std::endl;
  });

  // Initialize libcurl
  curl_global_init(CURL_GLOBAL_DEFAULT);

  // Create NQE instance
  nqe::Nqe::Options opts;
  opts.decay_lambda_per_sec = 0.02;
  
  std::string error;
  if (!nqe::Nqe::validateOptions(opts, &error)) {
    std::cerr << "Invalid options: " << error << std::endl;
    curl_global_cleanup();
    return 1;
  }

  nqe::Nqe estimator(opts);
  nqe::HttpRttSource http_source(estimator);

  // Test websites (popular sites from around the world)
  std::vector<std::string> test_urls = {
    // Search Engines & Tech Giants
    "https://www.google.com",
    "https://www.baidu.com",
    "https://www.bing.com",
    "https://www.yahoo.com",
    "https://www.yandex.com",
    
    // Social Media
    "https://www.facebook.com",
    "https://www.twitter.com",
    "https://www.instagram.com",
    "https://www.linkedin.com",
    "https://www.reddit.com",
    "https://www.tiktok.com",
    "https://www.weibo.com",
    
    // E-commerce
    "https://www.amazon.com",
    "https://www.alibaba.com",
    "https://www.taobao.com",
    "https://www.ebay.com",
    "https://www.jd.com",
    
    // Technology & Development
    "https://www.github.com",
    "https://www.stackoverflow.com",
    "https://www.gitlab.com",
    "https://www.npmjs.com",
    "https://www.pypi.org",
    
    // Cloud & CDN
    "https://www.cloudflare.com",
    "https://www.amazon.com",
    "https://www.microsoft.com",
    "https://aws.amazon.com",
    "https://cloud.google.com",
    
    // Media & Entertainment
    "https://www.youtube.com",
    "https://www.netflix.com",
    "https://www.twitch.tv",
    "https://www.spotify.com",
    "https://www.bilibili.com",
    
    // News & Information
    "https://www.wikipedia.org",
    "https://www.bbc.com",
    "https://www.cnn.com",
    "https://www.nytimes.com",
    "https://www.xinhuanet.com",
    
    // Education & Research
    "https://www.mit.edu",
    "https://www.stanford.edu",
    "https://www.coursera.org",
    "https://www.udemy.com",
    
    // Chinese Sites
    "https://www.qq.com",
    "https://www.163.com",
    "https://www.sina.com.cn",
    "https://www.sohu.com",
    "https://www.zhihu.com",
    
    // Others
    "https://www.apple.com",
    "https://www.adobe.com",
    "https://www.wordpress.com"
  };

  std::cout << "\n========================================" << std::endl;
  std::cout << "Testing Real Website HTTP RTT" << std::endl;
  std::cout << "Total URLs: " << test_urls.size() << std::endl;
  std::cout << "========================================\n" << std::endl;

  for (size_t i = 0; i < test_urls.size(); ++i) {
    const std::string& url = test_urls[i];
    uint64_t request_id = i;

    std::cout << "Request " << request_id << ": " << url << std::endl;

    // Create CURL handle
    CURL* curl = curl_easy_init();
    if (!curl) {
      std::cerr << "  [ERROR] Failed to initialize CURL" << std::endl;
      continue;
    }

    // Configure request
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);  // HEAD request only
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // Skip SSL verification for simplicity
    
    // Mark request sent
    http_source.onRequestSent(request_id);
    auto start_time = nqe::Clock::now();

    // Perform request
    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
      // Mark response received
      http_source.onResponseHeaders(request_id);
      auto end_time = nqe::Clock::now();
      
      double total_time = 0;
      curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_time);
      
      auto rtt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
      
      std::cout << "  ✓ Response received (actual: " << rtt_ms << "ms)" << std::endl;
      
      // Get current NQE estimate
      auto estimate = estimator.getEstimate();
      std::cout << "  NQE RTT estimate: " << estimate.rtt_ms << "ms";
      if (estimate.http_ttfb_ms) {
        std::cout << " (HTTP TTFB: " << *estimate.http_ttfb_ms << "ms)";
      }
      std::cout << std::endl;
      
      auto ect = estimator.getEffectiveConnectionType();
      std::cout << "  ECT: " << nqe::effectiveConnectionTypeToString(ect) << std::endl;
    } else {
      std::cout << "  ✗ Request failed: " << curl_easy_strerror(res) << std::endl;
    }

    curl_easy_cleanup(curl);
    std::cout << std::endl;
    
    // Brief pause between requests
    std::this_thread::sleep_for(200ms);
  }

  // Print final statistics
  std::cout << "\n========================================" << std::endl;
  std::cout << "Final Statistics" << std::endl;
  std::cout << "========================================" << std::endl;
  
  auto stats = estimator.getStatistics();
  std::cout << "Total samples: " << stats.total_samples << std::endl;
  
  if (stats.http.sample_count > 0) {
    std::cout << "\nHTTP TTFB Statistics:" << std::endl;
    std::cout << "  Sample count: " << stats.http.sample_count << std::endl;
    std::cout << "  Min: " << *stats.http.min_ms << "ms" << std::endl;
    std::cout << "  Median (p50): " << *stats.http.percentile_50th << "ms" << std::endl;
    std::cout << "  p95: " << *stats.http.percentile_95th << "ms" << std::endl;
    std::cout << "  Max: " << *stats.http.max_ms << "ms" << std::endl;
  }
  
  auto final_estimate = estimator.getEstimate();
  auto final_ect = estimator.getEffectiveConnectionType();
  
  std::cout << "\nFinal Network Quality:" << std::endl;
  std::cout << "  RTT: " << final_estimate.rtt_ms << "ms" << std::endl;
  if (final_estimate.http_ttfb_ms) {
    std::cout << "  HTTP TTFB: " << *final_estimate.http_ttfb_ms << "ms" << std::endl;
  }
  std::cout << "  ECT: " << nqe::effectiveConnectionTypeToString(final_ect) << std::endl;

  curl_global_cleanup();
  return 0;
}
