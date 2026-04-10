#pragma once
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>

namespace nqe {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

class Aggregator;

/**
 * ThroughputAnalyzer tracks downstream throughput from HTTP responses.
 * 
 * Implements Chromium's NQE throughput analysis with:
 * - Request tracking (active requests vs accuracy-degrading requests)
 * - Observation window mechanism
 * - Request filtering (method, size, caching, etc.)
 * - Minimum requests in flight threshold
 * 
 * Thread-safe: All methods can be called from multiple threads.
 */
class ThroughputAnalyzer {
public:
  struct Options {
    double decay_lambda_per_sec = 0.02;         ///< Sample decay rate
    std::chrono::seconds freshness_threshold{60}; ///< Max age before samples are stale
    size_t min_transfer_size_bytes = 32000;     ///< Minimum transfer size (32KB, ~32*8*1000 bits)
    size_t throughput_min_requests_in_flight = 5; ///< Minimum concurrent requests for observation window
    
    // Window-level hanging detection (Chrome NQE-style)
    bool enable_hanging_window_detection = true;  ///< Enable cwnd-based hanging window detection
    double hanging_window_cwnd_multiplier = 0.5;  ///< Multiplier for cwnd bits threshold
    size_t hanging_window_cwnd_size_kb = 10;      ///< Initial congestion window size in KB (typically 10)
  };
  
  /**
   * Represents an HTTP request being tracked for throughput analysis.
   */
  struct Request {
    const void* id;                    ///< Unique identifier (e.g., pointer to request object)
    uint64_t sequence_id = 0;          ///< Monotonic sequence number for age-based ordering
    std::string method;                ///< HTTP method (GET, POST, etc.)
    size_t bytes_received = 0;         ///< Total bytes received so far
    TimePoint start_time;              ///< When the request started
    bool is_cached = false;            ///< Whether response is from cache
    bool is_hanging = false;           ///< Whether request is hanging/stalled
    
    /// Order by sequence_id so oldest requests are evicted first
    bool operator<(const Request& other) const { return sequence_id < other.sequence_id; }
  };
  
  explicit ThroughputAnalyzer();
  explicit ThroughputAnalyzer(const Options& opts);
  ~ThroughputAnalyzer();
  
  /**
   * Notify that a transaction has started.
   * 
   * @param request Request information
   */
  void notifyStartTransaction(const Request& request);
  
  /**
   * Notify that bytes have been read for a request.
   * 
   * @param request_id Unique identifier for the request
   * @param bytes_read Number of new bytes read
   */
  void notifyBytesRead(const void* request_id, size_t bytes_read);
  
  /**
   * Notify that a request has completed.
   * 
   * @param request_id Unique identifier for the request
   */
  void notifyRequestCompleted(const void* request_id);
  
  /**
   * Handle network connection type change.
   * Resets current observation window and moves active requests to degrading set.
   */
  void onConnectionTypeChanged();
  
  /**
   * Record a completed data transfer (legacy simple API).
   * 
   * @param bytes_received Number of bytes received
   * @param start_time When the transfer started
   * @param end_time When the transfer completed
   */
  void addTransfer(size_t bytes_received, TimePoint start_time, TimePoint end_time);
  
  /**
   * Get current throughput estimate in kilobits per second.
   * 
   * @param now Current timestamp
   * @return Throughput in kbps, or nullopt if no data available
   */
  std::optional<double> getEstimate(TimePoint now = Clock::now()) const;
  
  /**
   * Get statistics about throughput samples.
   */
  struct Statistics {
    size_t sample_count = 0;
    std::optional<double> min_kbps;
    std::optional<double> max_kbps;
    std::optional<double> percentile_50th;
    std::optional<double> percentile_95th;
    std::optional<double> percentile_99th;
    TimePoint last_sample_time;
    size_t active_requests = 0;         ///< Number of currently active requests
    size_t degrading_requests = 0;      ///< Number of accuracy-degrading requests
  };
  
  Statistics getStatistics(TimePoint now = Clock::now()) const;
  
  /**
   * Get the number of throughput samples collected.
   */
  size_t getSampleCount() const;
  
  /**
   * Check if the current observation window is hanging (Chrome NQE-style).
   * A window is considered hanging if the bits received scaled to 1 HTTP RTT
   * is less than the congestion window size times a multiplier.
   * 
   * @param bits_received Bits received during the window
   * @param window_duration Duration of the window
   * @param http_rtt_ms HTTP RTT estimate in milliseconds
   * @return true if the window is hanging
   */
  bool isHangingWindow(
      size_t bits_received,
      std::chrono::milliseconds window_duration,
      double http_rtt_ms) const;
  
private:
  /**
   * Check if a request should be discarded from throughput calculation.
   * Returns true for non-GET requests.
   */
  bool shouldDiscardRequest(const Request& request) const;
  
  /**
   * Check if a request degrades accuracy.
   * Returns true for cached, hanging, or too-small requests.
   */
  bool degradesAccuracy(const Request& request) const;
  
  /**
   * Try to start a throughput observation window.
   * Window starts when: no degrading requests and enough requests in flight.
   */
  void maybeStartThroughputObservationWindow();
  
  /**
   * End the current throughput observation window.
   */
  void endThroughputObservationWindow();
  
  /**
   * Try to get a throughput observation from the current window.
   * Called when a request completes during an active window.
   */
  void maybeGetThroughputObservation();
  
  /**
   * Get total bits received across all active requests.
   */
  size_t getBitsReceived() const;
  
  // Constants for bit/byte conversion
  static constexpr size_t BITS_PER_BYTE = 8;
  
  Options opts_;
  std::unique_ptr<Aggregator> aggregator_;
  mutable std::mutex mu_;
  
  // Request tracking
  std::set<Request> requests_;                      ///< Active, accuracy-preserving requests
  std::set<Request> accuracy_degrading_requests_;   ///< Accuracy-degrading requests
  
  // Observation window tracking
  bool in_observation_window_ = false;
  TimePoint window_start_time_;
  size_t bits_received_at_window_start_ = 0;
  
  // Constants
  static constexpr size_t kMaxRequestsSize = 300;   ///< Maximum requests to track
  uint64_t next_sequence_id_ = 0;                     ///< Monotonic counter for Request ordering
};

} // namespace nqe
