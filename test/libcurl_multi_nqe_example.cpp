#include <curl/curl.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <iomanip>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "nqe/Nqe.h"
#include "nqe/Logger.h"
#include "nqe/EffectiveConnectionType.h"
#include "nqe/ReportGenerator.h"

using namespace std::chrono_literals;

static size_t sink_write(char* ptr, size_t size, size_t nmemb, void* userdata) {
  (void)ptr;
  // Track bytes received for throughput calculation
  if (userdata) {
    size_t bytes = size * nmemb;
    auto* total_bytes = static_cast<size_t*>(userdata);
    *total_bytes += bytes;
  }
  return size * nmemb;
}

struct CurlNqeCtx {
  nqe::Nqe* estimator;
  std::chrono::steady_clock::time_point start_time;
  size_t bytes_received;
};

// Register sockets created by libcurl for TCP_INFO sampling
static int sockopt_cb(void* clientp, curl_socket_t curlfd, curlsocktype purpose) {
  auto* ctx = static_cast<CurlNqeCtx*>(clientp);
  if (!ctx || !ctx->estimator) return 0;
  if (purpose == CURLSOCKTYPE_IPCXN) {
#ifdef _WIN32
    nqe::Nqe::SocketHandle sh = static_cast<nqe::Nqe::SocketHandle>(curlfd);
#else
    nqe::Nqe::SocketHandle sh = static_cast<nqe::Nqe::SocketHandle>(curlfd);
#endif
    ctx->estimator->registerTcpSocket(sh);
  }
  return 0;
}

// Unregister sockets before libcurl closes them
static int close_socket_cb(void* clientp, curl_socket_t item) {
  auto* ctx = static_cast<CurlNqeCtx*>(clientp);
  if (!ctx || !ctx->estimator) return 0;
#ifdef _WIN32
  nqe::Nqe::SocketHandle sh = static_cast<nqe::Nqe::SocketHandle>(item);
#else
  nqe::Nqe::SocketHandle sh = static_cast<nqe::Nqe::SocketHandle>(item);
#endif
  ctx->estimator->unregisterTcpSocket(sh);
  return 0;
}

static std::atomic<bool> g_stop{false};
static void on_signal(int) { g_stop.store(true); }

int main(int argc, char** argv) {
  std::signal(SIGINT, on_signal);

  // Setup logging
  nqe::Logger::instance().setMinLevel(nqe::LogLevel::LOG_INFO);
  nqe::Logger::instance().setCallback([](nqe::LogLevel level, const std::string& msg) {
    std::cout << "[" << nqe::getTimestamp() << "] [" 
              << nqe::logLevelToString(level) << "] " << msg << std::endl;
  });

  std::vector<std::string> urls;
  if (argc > 1) {
    for (int i = 1; i < argc; ++i) urls.emplace_back(argv[i]);
  } else {
    urls = {
      "https://example.com/",
      "https://www.google.com/",
      "https://www.cloudflare.com/"
    };
  }

  curl_global_init(CURL_GLOBAL_ALL);

  // Start tracking test data
  nqe::ReportGenerator::TestData test_data;
  test_data.test_start = std::chrono::steady_clock::now();
  test_data.urls = urls;

  nqe::Nqe::Options opts;
  opts.decay_lambda_per_sec = 0.03;
  opts.transport_sample_period = 1000ms;
  opts.combine_bias_to_lower = 0.65;
  opts.freshness_threshold = std::chrono::seconds(60);

  // Validate options before creating the estimator
  std::string validation_error;
  if (!nqe::Nqe::validateOptions(opts, &validation_error)) {
    std::cerr << "Invalid NQE options: " << validation_error << std::endl;
    return 1;
  }

  // Store options in test data
  test_data.options = opts;

  nqe::Nqe estimator(opts);
  estimator.startTransportSampler();

  // Periodically print estimates and statistics
  std::thread poller([&]{
    int counter = 0;
    while (!g_stop.load()) {
      auto e = estimator.getEstimate();
      std::cout << std::fixed << std::setprecision(1);
      std::cout << "[NQE] rtt=" << e.rtt_ms
                << "ms http=" << (e.http_ttfb_ms ? *e.http_ttfb_ms : -1)
                << " tr="     << (e.transport_rtt_ms ? *e.transport_rtt_ms : -1)
                << " ping="   << (e.ping_rtt_ms ? *e.ping_rtt_ms : -1);
      if (e.throughput_kbps) {
        std::cout << " throughput=" << std::setprecision(0) << *e.throughput_kbps << "kbps";
      }
      std::cout << " ECT=" << nqe::effectiveConnectionTypeToString(e.effective_type)
                << std::endl;
      
      // Print detailed statistics every 5 seconds
      if (++counter % 5 == 0) {
        auto stats = estimator.getStatistics();
        std::cout << "[STATS] Total samples: " << stats.total_samples
                  << " | Active sockets: " << stats.active_sockets
                  << " | ECT: " << nqe::effectiveConnectionTypeToString(stats.effective_type) << "\n";
        
        if (stats.http.sample_count > 0) {
          std::cout << std::fixed << std::setprecision(1);
          std::cout << "  HTTP: count=" << stats.http.sample_count
                    << " min=" << (stats.http.min_ms ? *stats.http.min_ms : -1)
                    << " p50=" << (stats.http.percentile_50th ? *stats.http.percentile_50th : -1)
                    << " p95=" << (stats.http.percentile_95th ? *stats.http.percentile_95th : -1)
                    << " max=" << (stats.http.max_ms ? *stats.http.max_ms : -1) << "\n";
        }
        
        if (stats.transport.sample_count > 0) {
          std::cout << std::fixed << std::setprecision(1);
          std::cout << "  Transport: count=" << stats.transport.sample_count
                    << " min=" << (stats.transport.min_ms ? *stats.transport.min_ms : -1)
                    << " p50=" << (stats.transport.percentile_50th ? *stats.transport.percentile_50th : -1)
                    << " p95=" << (stats.transport.percentile_95th ? *stats.transport.percentile_95th : -1)
                    << " max=" << (stats.transport.max_ms ? *stats.transport.max_ms : -1) << "\n";
        }
        
        if (stats.throughput.sample_count > 0) {
          std::cout << std::fixed << std::setprecision(0);
          std::cout << "  Throughput: count=" << stats.throughput.sample_count
                    << " min=" << (stats.throughput.min_kbps ? *stats.throughput.min_kbps : -1)
                    << " p50=" << (stats.throughput.percentile_50th ? *stats.throughput.percentile_50th : -1)
                    << " p95=" << (stats.throughput.percentile_95th ? *stats.throughput.percentile_95th : -1)
                    << " max=" << (stats.throughput.max_kbps ? *stats.throughput.max_kbps : -1) << "kbps\n";
        }
        
        std::cout << "  Sampler running: " << (estimator.isTransportSamplerRunning() ? "yes" : "no") << "\n";
      }
      
      std::this_thread::sleep_for(1000ms);
    }
  });

  CURLM* multi = curl_multi_init();
  if (!multi) {
    std::cerr << "Failed to init CURLM\n";
    return 1;
  }

  // Create easy handles
  struct EasyCtx {
    CURL* eh;
    std::string url;
    CurlNqeCtx nqe_ctx;
  };
  std::vector<std::unique_ptr<EasyCtx>> easies;
  easies.reserve(urls.size());

  for (auto& url : urls) {
    auto e = std::make_unique<EasyCtx>();
    e->eh = curl_easy_init();
    e->url = url;
    e->nqe_ctx.estimator = &estimator;
    e->nqe_ctx.start_time = nqe::Clock::now();
    e->nqe_ctx.bytes_received = 0;
    
    curl_easy_setopt(e->eh, CURLOPT_URL, e->url.c_str());
    curl_easy_setopt(e->eh, CURLOPT_WRITEFUNCTION, sink_write);
    curl_easy_setopt(e->eh, CURLOPT_WRITEDATA, &e->nqe_ctx.bytes_received);
    curl_easy_setopt(e->eh, CURLOPT_FOLLOWLOCATION, 1L);
#ifdef CURL_HTTP_VERSION_2TLS
    curl_easy_setopt(e->eh, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
#endif
    // Register socket callbacks on each easy handle
    curl_easy_setopt(e->eh, CURLOPT_SOCKOPTFUNCTION, sockopt_cb);
    curl_easy_setopt(e->eh, CURLOPT_SOCKOPTDATA, &e->nqe_ctx);
    curl_easy_setopt(e->eh, CURLOPT_CLOSESOCKETFUNCTION, close_socket_cb);
    curl_easy_setopt(e->eh, CURLOPT_CLOSESOCKETDATA, &e->nqe_ctx);

    curl_multi_add_handle(multi, e->eh);
    easies.emplace_back(std::move(e));
  }

  int still_running = 0;
  CURLMcode mcode = curl_multi_perform(multi, &still_running);
  if (mcode != CURLM_OK) {
    std::cerr << "curl_multi_perform init failed: " << curl_multi_strerror(mcode) << "\n";
  }

  while (!g_stop.load() && still_running) {
    int numfds = 0;
    mcode = curl_multi_poll(multi, nullptr, 0, 1000, &numfds);
    if (mcode != CURLM_OK) {
      std::cerr << "curl_multi_poll error: " << curl_multi_strerror(mcode) << "\n";
      break;
    }

    mcode = curl_multi_perform(multi, &still_running);
    if (mcode != CURLM_OK) {
      std::cerr << "curl_multi_perform error: " << curl_multi_strerror(mcode) << "\n";
      break;
    }

    // Read completed transfers
    int msgs_left = 0;
    while (CURLMsg* msg = curl_multi_info_read(multi, &msgs_left)) {
      if (msg->msg == CURLMSG_DONE) {
        CURL* eh = msg->easy_handle;

        char* eff_url = nullptr;
        curl_easy_getinfo(eh, CURLINFO_EFFECTIVE_URL, &eff_url);
        std::string url_str = eff_url ? eff_url : "(null)";

        nqe::ReportGenerator::TestData::UrlResult result;
        result.url = url_str;

        if (msg->data.result != CURLE_OK) {
          std::cerr << "[DONE] " << url_str
                    << " failed: " << curl_easy_strerror(msg->data.result) << "\n";
          result.success = false;
          result.error_msg = curl_easy_strerror(msg->data.result);
          result.ttfb_ms = 0.0;
        } else {
          // Find the EasyCtx for this handle to get throughput data
          EasyCtx* ctx = nullptr;
          for (auto& e : easies) {
            if (e->eh == eh) {
              ctx = e.get();
              break;
            }
          }
          
          double ttfb_sec = 0.0;
          if (curl_easy_getinfo(eh, CURLINFO_STARTTRANSFER_TIME, &ttfb_sec) == CURLE_OK && ttfb_sec > 0.0) {
            double ttfb_ms = ttfb_sec * 1000.0;
            estimator.addSample(nqe::Source::HTTP_TTFB, ttfb_ms, nqe::Clock::now());
            std::cout << "[HTTP] TTFB(ms) " << url_str << " = " << ttfb_ms << "\n";
            result.success = true;
            result.ttfb_ms = ttfb_ms;
          } else {
            std::cout << "[HTTP] TTFB unavailable for " << url_str << "\n";
            result.success = false;
            result.error_msg = "TTFB unavailable";
            result.ttfb_ms = 0.0;
          }
          
          // Add throughput sample if we have data
          if (ctx && ctx->nqe_ctx.bytes_received > 0) {
            auto end_time = nqe::Clock::now();
            estimator.addThroughputSample(
                ctx->nqe_ctx.bytes_received,
                ctx->nqe_ctx.start_time,
                end_time);
            
            auto duration = std::chrono::duration<double>(end_time - ctx->nqe_ctx.start_time).count();
            double throughput_kbps = (ctx->nqe_ctx.bytes_received * 8.0) / (duration * 1000.0);
            std::cout << "[THROUGHPUT] " << (eff_url ? eff_url : "(null)")
                      << " = " << std::fixed << std::setprecision(0) << throughput_kbps << " kbps "
                      << "(" << ctx->nqe_ctx.bytes_received << " bytes in "
                      << std::setprecision(2) << duration << " sec)\n";
          }
        }

        test_data.url_results.push_back(result);

        curl_multi_remove_handle(multi, eh);
        curl_easy_cleanup(eh);
      }
    }
  }

  // Cleanup remaining handles if any
  for (auto& e : easies) {
    if (e->eh) {
      curl_multi_remove_handle(multi, e->eh);
      curl_easy_cleanup(e->eh);
      e->eh = nullptr;
    }
  }
  curl_multi_cleanup(multi);
  curl_global_cleanup();

  g_stop.store(true);
  if (poller.joinable()) poller.join();

  estimator.stopTransportSampler();

  // Capture final statistics and generate report
  test_data.test_end = std::chrono::steady_clock::now();
  test_data.final_stats = estimator.getStatistics();
  test_data.final_estimate = estimator.getEstimate();

  std::cout << "\n=== Generating Test Report ===\n";

  // Generate HTML report
  if (nqe::ReportGenerator::generateHtmlReport(test_data, "nqe_test_report.html")) {
    std::cout << "HTML report generated: nqe_test_report.html\n";
  } else {
    std::cerr << "Failed to generate HTML report\n";
  }

  // Generate text report
  if (nqe::ReportGenerator::generateTextReport(test_data, "nqe_test_report.txt")) {
    std::cout << "Text report generated: nqe_test_report.txt\n";
  } else {
    std::cerr << "Failed to generate text report\n";
  }

  std::cout << "=== Report Generation Complete ===\n\n";

  return 0;
}