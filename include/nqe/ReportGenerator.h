#pragma once
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include "Nqe.h"
#include "Logger.h"

namespace nqe {

/**
 * Report Generator for NQE Test Results
 * 
 * Header-only utility class with static methods for generating
 * comprehensive HTML and text reports from NQE test data.
 * Includes statistics, configuration, and test metadata.
 */
class ReportGenerator {
public:
    struct TestData {
        std::vector<std::string> urls;
        Nqe::Options options;
        Statistics final_stats;
        Estimate final_estimate;
        std::chrono::steady_clock::time_point test_start;
        std::chrono::steady_clock::time_point test_end;
        
        // Additional tracking data
        struct UrlResult {
            std::string url;
            bool success;
            std::string error_msg;
            double ttfb_ms;
        };
        std::vector<UrlResult> url_results;
    };
    
    /**
     * Generate an HTML report
     * 
     * @param data Test data collected during the NQE test
     * @param filepath Path to save the HTML report
     * @return true if report was successfully generated, false otherwise
     */
    static bool generateHtmlReport(const TestData& data, const std::string& filepath) {
        std::ofstream out(filepath);
        if (!out.is_open()) {
            return false;
        }
        
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            data.test_end - data.test_start);
        
        out << "<!DOCTYPE html>\n";
        out << "<html lang=\"en\">\n";
        out << "<head>\n";
        out << "  <meta charset=\"UTF-8\">\n";
        out << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
        out << "  <title>NQE Test Report</title>\n";
        out << "  <style>\n";
        out << "    body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }\n";
        out << "    .container { max-width: 1200px; margin: 0 auto; background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }\n";
        out << "    h1 { color: #333; border-bottom: 3px solid #007bff; padding-bottom: 10px; }\n";
        out << "    h2 { color: #555; margin-top: 30px; border-bottom: 2px solid #ddd; padding-bottom: 5px; }\n";
        out << "    h3 { color: #666; margin-top: 20px; }\n";
        out << "    table { width: 100%; border-collapse: collapse; margin: 20px 0; }\n";
        out << "    th, td { padding: 12px; text-align: left; border: 1px solid #ddd; }\n";
        out << "    th { background-color: #007bff; color: white; font-weight: bold; }\n";
        out << "    tr:nth-child(even) { background-color: #f9f9f9; }\n";
        out << "    .metric { display: inline-block; margin: 10px 20px 10px 0; padding: 15px; background: #e9ecef; border-radius: 5px; min-width: 200px; }\n";
        out << "    .metric-label { font-size: 14px; color: #666; margin-bottom: 5px; }\n";
        out << "    .metric-value { font-size: 24px; font-weight: bold; color: #007bff; }\n";
        out << "    .success { color: #28a745; }\n";
        out << "    .error { color: #dc3545; }\n";
        out << "    .timestamp { color: #6c757d; font-size: 14px; }\n";
        out << "  </style>\n";
        out << "</head>\n";
        out << "<body>\n";
        out << "  <div class=\"container\">\n";
        
        // Title and timestamp
        out << "    <h1>Network Quality Estimator (NQE) Test Report</h1>\n";
        out << "    <p class=\"timestamp\">Generated: " << getCurrentTimestamp() << "</p>\n";
        
        // Test Summary
        out << "    <h2>Test Summary</h2>\n";
        out << "    <div class=\"metric\">\n";
        out << "      <div class=\"metric-label\">Test Duration</div>\n";
        out << "      <div class=\"metric-value\">" << duration.count() << "s</div>\n";
        out << "    </div>\n";
        out << "    <div class=\"metric\">\n";
        out << "      <div class=\"metric-label\">URLs Tested</div>\n";
        out << "      <div class=\"metric-value\">" << data.urls.size() << "</div>\n";
        out << "    </div>\n";
        out << "    <div class=\"metric\">\n";
        out << "      <div class=\"metric-label\">Total Samples</div>\n";
        out << "      <div class=\"metric-value\">" << data.final_stats.total_samples << "</div>\n";
        out << "    </div>\n";
        out << "    <div class=\"metric\">\n";
        out << "      <div class=\"metric-label\">Active Sockets</div>\n";
        out << "      <div class=\"metric-value\">" << data.final_stats.active_sockets << "</div>\n";
        out << "    </div>\n";
        
        // Final RTT Estimate
        out << "    <h2>Final RTT Estimate</h2>\n";
        out << "    <div class=\"metric\">\n";
        out << "      <div class=\"metric-label\">Combined RTT</div>\n";
        out << "      <div class=\"metric-value\">" << std::fixed << std::setprecision(2) 
            << data.final_estimate.rtt_ms << " ms</div>\n";
        out << "    </div>\n";
        if (data.final_estimate.http_ttfb_ms) {
            out << "    <div class=\"metric\">\n";
            out << "      <div class=\"metric-label\">HTTP TTFB</div>\n";
            out << "      <div class=\"metric-value\">" << std::fixed << std::setprecision(2)
                << *data.final_estimate.http_ttfb_ms << " ms</div>\n";
            out << "    </div>\n";
        }
        if (data.final_estimate.transport_rtt_ms) {
            out << "    <div class=\"metric\">\n";
            out << "      <div class=\"metric-label\">Transport RTT</div>\n";
            out << "      <div class=\"metric-value\">" << std::fixed << std::setprecision(2)
                << *data.final_estimate.transport_rtt_ms << " ms</div>\n";
            out << "    </div>\n";
        }
        if (data.final_estimate.ping_rtt_ms) {
            out << "    <div class=\"metric\">\n";
            out << "      <div class=\"metric-label\">PING RTT</div>\n";
            out << "      <div class=\"metric-value\">" << std::fixed << std::setprecision(2)
                << *data.final_estimate.ping_rtt_ms << " ms</div>\n";
            out << "    </div>\n";
        }
        
        // URL Results
        if (!data.url_results.empty()) {
            out << "    <h2>URL Test Results</h2>\n";
            out << "    <table>\n";
            out << "      <tr><th>URL</th><th>Status</th><th>TTFB (ms)</th><th>Notes</th></tr>\n";
            for (const auto& result : data.url_results) {
                out << "      <tr>\n";
                out << "        <td>" << escapeHtml(result.url) << "</td>\n";
                out << "        <td class=\"" << (result.success ? "success" : "error") << "\">"
                    << (result.success ? "✓ Success" : "✗ Failed") << "</td>\n";
                out << "        <td>" << (result.success ? std::to_string(result.ttfb_ms) : "N/A") << "</td>\n";
                out << "        <td>" << (result.success ? "" : escapeHtml(result.error_msg)) << "</td>\n";
                out << "      </tr>\n";
            }
            out << "    </table>\n";
        }
        
        // Statistics by Source
        out << "    <h2>Statistics by Source</h2>\n";
        
        // HTTP Statistics
        if (data.final_stats.http.sample_count > 0) {
            out << "    <h3>HTTP TTFB Statistics</h3>\n";
            out << "    <table>\n";
            out << "      <tr><th>Metric</th><th>Value (ms)</th></tr>\n";
            out << "      <tr><td>Sample Count</td><td>" << data.final_stats.http.sample_count << "</td></tr>\n";
            if (data.final_stats.http.min_ms) {
                out << "      <tr><td>Minimum</td><td>" << std::fixed << std::setprecision(2) 
                    << *data.final_stats.http.min_ms << "</td></tr>\n";
            }
            if (data.final_stats.http.percentile_50th) {
                out << "      <tr><td>50th Percentile (Median)</td><td>" << std::fixed << std::setprecision(2)
                    << *data.final_stats.http.percentile_50th << "</td></tr>\n";
            }
            if (data.final_stats.http.percentile_95th) {
                out << "      <tr><td>95th Percentile</td><td>" << std::fixed << std::setprecision(2)
                    << *data.final_stats.http.percentile_95th << "</td></tr>\n";
            }
            if (data.final_stats.http.percentile_99th) {
                out << "      <tr><td>99th Percentile</td><td>" << std::fixed << std::setprecision(2)
                    << *data.final_stats.http.percentile_99th << "</td></tr>\n";
            }
            if (data.final_stats.http.max_ms) {
                out << "      <tr><td>Maximum</td><td>" << std::fixed << std::setprecision(2)
                    << *data.final_stats.http.max_ms << "</td></tr>\n";
            }
            out << "    </table>\n";
        }
        
        // Transport Statistics
        if (data.final_stats.transport.sample_count > 0) {
            out << "    <h3>Transport RTT Statistics</h3>\n";
            out << "    <table>\n";
            out << "      <tr><th>Metric</th><th>Value (ms)</th></tr>\n";
            out << "      <tr><td>Sample Count</td><td>" << data.final_stats.transport.sample_count << "</td></tr>\n";
            if (data.final_stats.transport.min_ms) {
                out << "      <tr><td>Minimum</td><td>" << std::fixed << std::setprecision(2)
                    << *data.final_stats.transport.min_ms << "</td></tr>\n";
            }
            if (data.final_stats.transport.percentile_50th) {
                out << "      <tr><td>50th Percentile (Median)</td><td>" << std::fixed << std::setprecision(2)
                    << *data.final_stats.transport.percentile_50th << "</td></tr>\n";
            }
            if (data.final_stats.transport.percentile_95th) {
                out << "      <tr><td>95th Percentile</td><td>" << std::fixed << std::setprecision(2)
                    << *data.final_stats.transport.percentile_95th << "</td></tr>\n";
            }
            if (data.final_stats.transport.percentile_99th) {
                out << "      <tr><td>99th Percentile</td><td>" << std::fixed << std::setprecision(2)
                    << *data.final_stats.transport.percentile_99th << "</td></tr>\n";
            }
            if (data.final_stats.transport.max_ms) {
                out << "      <tr><td>Maximum</td><td>" << std::fixed << std::setprecision(2)
                    << *data.final_stats.transport.max_ms << "</td></tr>\n";
            }
            out << "    </table>\n";
        }
        
        // PING Statistics
        if (data.final_stats.ping.sample_count > 0) {
            out << "    <h3>PING RTT Statistics</h3>\n";
            out << "    <table>\n";
            out << "      <tr><th>Metric</th><th>Value (ms)</th></tr>\n";
            out << "      <tr><td>Sample Count</td><td>" << data.final_stats.ping.sample_count << "</td></tr>\n";
            if (data.final_stats.ping.min_ms) {
                out << "      <tr><td>Minimum</td><td>" << std::fixed << std::setprecision(2)
                    << *data.final_stats.ping.min_ms << "</td></tr>\n";
            }
            if (data.final_stats.ping.percentile_50th) {
                out << "      <tr><td>50th Percentile (Median)</td><td>" << std::fixed << std::setprecision(2)
                    << *data.final_stats.ping.percentile_50th << "</td></tr>\n";
            }
            if (data.final_stats.ping.percentile_95th) {
                out << "      <tr><td>95th Percentile</td><td>" << std::fixed << std::setprecision(2)
                    << *data.final_stats.ping.percentile_95th << "</td></tr>\n";
            }
            if (data.final_stats.ping.percentile_99th) {
                out << "      <tr><td>99th Percentile</td><td>" << std::fixed << std::setprecision(2)
                    << *data.final_stats.ping.percentile_99th << "</td></tr>\n";
            }
            if (data.final_stats.ping.max_ms) {
                out << "      <tr><td>Maximum</td><td>" << std::fixed << std::setprecision(2)
                    << *data.final_stats.ping.max_ms << "</td></tr>\n";
            }
            out << "    </table>\n";
        }
        
        // Configuration
        out << "    <h2>Test Configuration</h2>\n";
        out << "    <table>\n";
        out << "      <tr><th>Parameter</th><th>Value</th></tr>\n";
        out << "      <tr><td>Decay Lambda (per second)</td><td>" << data.options.decay_lambda_per_sec << "</td></tr>\n";
        out << "      <tr><td>Transport Sample Period</td><td>" 
            << data.options.transport_sample_period.count() << " ms</td></tr>\n";
        out << "      <tr><td>Combine Bias to Lower</td><td>" << data.options.combine_bias_to_lower << "</td></tr>\n";
        out << "      <tr><td>Freshness Threshold</td><td>" 
            << data.options.freshness_threshold.count() << " seconds</td></tr>\n";
        out << "    </table>\n";
        
        // URLs Tested
        out << "    <h2>URLs Tested</h2>\n";
        out << "    <table>\n";
        out << "      <tr><th>#</th><th>URL</th></tr>\n";
        int idx = 1;
        for (const auto& url : data.urls) {
            out << "      <tr><td>" << idx++ << "</td><td>" << escapeHtml(url) << "</td></tr>\n";
        }
        out << "    </table>\n";
        
        out << "  </div>\n";
        out << "</body>\n";
        out << "</html>\n";
        
        out.close();
        return true;
    }
    
    /**
     * Generate a text report
     * 
     * @param data Test data collected during the NQE test
     * @param filepath Path to save the text report
     * @return true if report was successfully generated, false otherwise
     */
    static bool generateTextReport(const TestData& data, const std::string& filepath) {
        std::ofstream out(filepath);
        if (!out.is_open()) {
            return false;
        }
        
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            data.test_end - data.test_start);
        
        out << "================================================================================\n";
        out << "           Network Quality Estimator (NQE) Test Report\n";
        out << "================================================================================\n";
        out << "Generated: " << getCurrentTimestamp() << "\n\n";
        
        // Test Summary
        out << "TEST SUMMARY\n";
        out << "------------\n";
        out << "Test Duration:     " << duration.count() << " seconds\n";
        out << "URLs Tested:       " << data.urls.size() << "\n";
        out << "Total Samples:     " << data.final_stats.total_samples << "\n";
        out << "Active Sockets:    " << data.final_stats.active_sockets << "\n\n";
        
        // Final RTT Estimate
        out << "FINAL RTT ESTIMATE\n";
        out << "------------------\n";
        out << std::fixed << std::setprecision(2);
        out << "Combined RTT:      " << data.final_estimate.rtt_ms << " ms\n";
        if (data.final_estimate.http_ttfb_ms) {
            out << "HTTP TTFB:         " << *data.final_estimate.http_ttfb_ms << " ms\n";
        }
        if (data.final_estimate.transport_rtt_ms) {
            out << "Transport RTT:     " << *data.final_estimate.transport_rtt_ms << " ms\n";
        }
        if (data.final_estimate.ping_rtt_ms) {
            out << "PING RTT:          " << *data.final_estimate.ping_rtt_ms << " ms\n";
        }
        out << "\n";
        
        // URL Results
        if (!data.url_results.empty()) {
            out << "URL TEST RESULTS\n";
            out << "----------------\n";
            for (const auto& result : data.url_results) {
                out << "URL: " << result.url << "\n";
                out << "  Status: " << (result.success ? "SUCCESS" : "FAILED") << "\n";
                if (result.success) {
                    out << "  TTFB:   " << result.ttfb_ms << " ms\n";
                } else {
                    out << "  Error:  " << result.error_msg << "\n";
                }
            }
            out << "\n";
        }
        
        // Statistics by Source
        out << "STATISTICS BY SOURCE\n";
        out << "--------------------\n\n";
        
        // HTTP Statistics
        if (data.final_stats.http.sample_count > 0) {
            out << "HTTP TTFB Statistics:\n";
            out << "  Sample Count:      " << data.final_stats.http.sample_count << "\n";
            if (data.final_stats.http.min_ms) {
                out << "  Minimum:           " << *data.final_stats.http.min_ms << " ms\n";
            }
            if (data.final_stats.http.percentile_50th) {
                out << "  50th Percentile:   " << *data.final_stats.http.percentile_50th << " ms\n";
            }
            if (data.final_stats.http.percentile_95th) {
                out << "  95th Percentile:   " << *data.final_stats.http.percentile_95th << " ms\n";
            }
            if (data.final_stats.http.percentile_99th) {
                out << "  99th Percentile:   " << *data.final_stats.http.percentile_99th << " ms\n";
            }
            if (data.final_stats.http.max_ms) {
                out << "  Maximum:           " << *data.final_stats.http.max_ms << " ms\n";
            }
            out << "\n";
        }
        
        // Transport Statistics
        if (data.final_stats.transport.sample_count > 0) {
            out << "Transport RTT Statistics:\n";
            out << "  Sample Count:      " << data.final_stats.transport.sample_count << "\n";
            if (data.final_stats.transport.min_ms) {
                out << "  Minimum:           " << *data.final_stats.transport.min_ms << " ms\n";
            }
            if (data.final_stats.transport.percentile_50th) {
                out << "  50th Percentile:   " << *data.final_stats.transport.percentile_50th << " ms\n";
            }
            if (data.final_stats.transport.percentile_95th) {
                out << "  95th Percentile:   " << *data.final_stats.transport.percentile_95th << " ms\n";
            }
            if (data.final_stats.transport.percentile_99th) {
                out << "  99th Percentile:   " << *data.final_stats.transport.percentile_99th << " ms\n";
            }
            if (data.final_stats.transport.max_ms) {
                out << "  Maximum:           " << *data.final_stats.transport.max_ms << " ms\n";
            }
            out << "\n";
        }
        
        // PING Statistics
        if (data.final_stats.ping.sample_count > 0) {
            out << "PING RTT Statistics:\n";
            out << "  Sample Count:      " << data.final_stats.ping.sample_count << "\n";
            if (data.final_stats.ping.min_ms) {
                out << "  Minimum:           " << *data.final_stats.ping.min_ms << " ms\n";
            }
            if (data.final_stats.ping.percentile_50th) {
                out << "  50th Percentile:   " << *data.final_stats.ping.percentile_50th << " ms\n";
            }
            if (data.final_stats.ping.percentile_95th) {
                out << "  95th Percentile:   " << *data.final_stats.ping.percentile_95th << " ms\n";
            }
            if (data.final_stats.ping.percentile_99th) {
                out << "  99th Percentile:   " << *data.final_stats.ping.percentile_99th << " ms\n";
            }
            if (data.final_stats.ping.max_ms) {
                out << "  Maximum:           " << *data.final_stats.ping.max_ms << " ms\n";
            }
            out << "\n";
        }
        
        // Configuration
        out << "TEST CONFIGURATION\n";
        out << "------------------\n";
        out << "Decay Lambda (per second):  " << data.options.decay_lambda_per_sec << "\n";
        out << "Transport Sample Period:    " << data.options.transport_sample_period.count() << " ms\n";
        out << "Combine Bias to Lower:      " << data.options.combine_bias_to_lower << "\n";
        out << "Freshness Threshold:        " << data.options.freshness_threshold.count() << " seconds\n\n";
        
        // URLs Tested
        out << "URLS TESTED\n";
        out << "-----------\n";
        int idx = 1;
        for (const auto& url : data.urls) {
            out << idx++ << ". " << url << "\n";
        }
        
        out << "\n================================================================================\n";
        out << "                            End of Report\n";
        out << "================================================================================\n";
        
        out.close();
        return true;
    }

private:
    static std::string getCurrentTimestamp() {
        // Reuse the timestamp function from Logger.h
        return getTimestamp();
    }
    
    static std::string escapeHtml(const std::string& str) {
        std::string result;
        result.reserve(str.size());
        for (char c : str) {
            switch (c) {
                case '<': result += "&lt;"; break;
                case '>': result += "&gt;"; break;
                case '&': result += "&amp;"; break;
                case '"': result += "&quot;"; break;
                case '\'': result += "&#39;"; break;
                default: result += c; break;
            }
        }
        return result;
    }
};

} // namespace nqe
