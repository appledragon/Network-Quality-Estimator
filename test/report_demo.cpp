#include <chrono>
#include <iostream>
#include <thread>
#include "nqe/Nqe.h"
#include "nqe/Logger.h"
#include "nqe/ReportGenerator.h"

using namespace std::chrono_literals;

int main() {
  // Setup logging
  nqe::Logger::instance().setMinLevel(nqe::LogLevel::LOG_INFO);
  nqe::Logger::instance().setCallback([](nqe::LogLevel level, const std::string& msg) {
    std::cout << "[" << nqe::getTimestamp() << "] [" 
              << nqe::logLevelToString(level) << "] " << msg << std::endl;
  });

  // Create test data
  nqe::ReportGenerator::TestData test_data;
  test_data.test_start = std::chrono::steady_clock::now();
  test_data.urls = {
    "https://example.com/",
    "https://www.google.com/",
    "https://www.cloudflare.com/"
  };

  // Configure NQE
  nqe::Nqe::Options opts;
  opts.decay_lambda_per_sec = 0.03;
  opts.transport_sample_period = 1000ms;
  opts.combine_bias_to_lower = 0.65;
  opts.freshness_threshold = std::chrono::seconds(60);

  // Validate options
  std::string validation_error;
  if (!nqe::Nqe::validateOptions(opts, &validation_error)) {
    std::cerr << "Invalid NQE options: " << validation_error << std::endl;
    return 1;
  }

  test_data.options = opts;

  // Create estimator
  nqe::Nqe estimator(opts);
  estimator.startTransportSampler();

  std::cout << "Simulating website tests with mock data...\n";

  // Simulate some HTTP TTFB samples
  std::vector<double> ttfb_samples = {120.5, 110.3, 115.8, 108.2, 125.1};
  for (double ttfb : ttfb_samples) {
    estimator.addSample(nqe::Source::HTTP_TTFB, ttfb);
    std::this_thread::sleep_for(100ms);
  }

  // Simulate some transport RTT samples
  std::vector<double> transport_samples = {18.2, 20.1, 19.5, 17.8, 21.0};
  for (double rtt : transport_samples) {
    estimator.addSample(nqe::Source::TRANSPORT_RTT, rtt);
    std::this_thread::sleep_for(100ms);
  }

  // Add mock URL results
  test_data.url_results = {
    {"https://example.com/", true, "", 120.5},
    {"https://www.google.com/", true, "", 110.3},
    {"https://www.cloudflare.com/", true, "", 115.8}
  };

  // Wait a bit for samples to be processed
  std::this_thread::sleep_for(500ms);

  // Stop the sampler
  estimator.stopTransportSampler();

  // Capture final statistics
  test_data.test_end = std::chrono::steady_clock::now();
  test_data.final_stats = estimator.getStatistics();
  test_data.final_estimate = estimator.getEstimate();

  std::cout << "\n=== Generating Test Report ===\n";

  // Generate HTML report
  if (nqe::ReportGenerator::generateHtmlReport(test_data, "nqe_demo_report.html")) {
    std::cout << "HTML report generated: nqe_demo_report.html\n";
  } else {
    std::cerr << "Failed to generate HTML report\n";
  }

  // Generate text report
  if (nqe::ReportGenerator::generateTextReport(test_data, "nqe_demo_report.txt")) {
    std::cout << "Text report generated: nqe_demo_report.txt\n";
  } else {
    std::cerr << "Failed to generate text report\n";
  }

  std::cout << "=== Report Generation Complete ===\n";

  return 0;
}
