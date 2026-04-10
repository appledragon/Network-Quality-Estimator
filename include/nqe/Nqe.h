#pragma once
#include "nqe/EffectiveConnectionType.h"
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace nqe {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

class RTTObserver;
class ThroughputObserver;
class EffectiveConnectionTypeObserver;
class NetworkID;
class NetworkQualityStore;

/// Combined RTT estimate with source-specific values
struct Estimate {
  double rtt_ms = 0.0;                          ///< Combined RTT estimate
  std::optional<double> http_ttfb_ms;           ///< HTTP TTFB (upper bound)
  std::optional<double> transport_rtt_ms;       ///< Transport RTT (lower bound)
  std::optional<double> ping_rtt_ms;            ///< PING RTT (lower bound)
  std::optional<double> end_to_end_rtt_ms;      ///< End-to-End RTT (from H2/H3 PING frames)
  std::optional<double> throughput_kbps;        ///< Downstream throughput in kbps
  EffectiveConnectionType effective_type = EffectiveConnectionType::UNKNOWN; ///< Connection quality classification
  TimePoint as_of;                              ///< Timestamp of this estimate
};

/// Statistics for a single RTT source
struct SourceStatistics {
  size_t sample_count = 0;                      ///< Number of samples collected
  std::optional<double> min_ms;                 ///< Minimum RTT observed
  std::optional<double> max_ms;                 ///< Maximum RTT observed
  std::optional<double> percentile_50th;        ///< Median (50th percentile)
  std::optional<double> percentile_95th;        ///< 95th percentile
  std::optional<double> percentile_99th;        ///< 99th percentile
  TimePoint last_sample_time;                   ///< Timestamp of last sample
};

/// Throughput statistics
struct ThroughputStatistics {
  size_t sample_count = 0;
  std::optional<double> min_kbps;
  std::optional<double> max_kbps;
  std::optional<double> percentile_50th;
  std::optional<double> percentile_95th;
  std::optional<double> percentile_99th;
  TimePoint last_sample_time;
};

/// Overall statistics across all sources
struct Statistics {
  SourceStatistics http;                        ///< HTTP TTFB statistics
  SourceStatistics transport;                   ///< Transport RTT statistics
  SourceStatistics ping;                        ///< PING RTT statistics
  SourceStatistics end_to_end;                  ///< End-to-End RTT statistics (H2/H3 PING)
  ThroughputStatistics throughput;              ///< Throughput statistics
  size_t total_samples = 0;                     ///< Total samples across all sources
  size_t active_sockets = 0;                    ///< Number of registered sockets
  EffectiveConnectionType effective_type = EffectiveConnectionType::UNKNOWN; ///< Current ECT
};

/// RTT sample source type
enum class Source {
  HTTP_TTFB,      ///< HTTP Time-To-First-Byte (upper bound)
  TRANSPORT_RTT,  ///< TCP transport layer RTT from TCP_INFO (lower bound)
  PING_RTT,       ///< QUIC/H2 PING RTT (lower bound)
  
  // More granular sources (Chrome NQE compatibility)
  TCP,            ///< TCP transport RTT
  QUIC,           ///< QUIC transport RTT
  H2_PINGS,       ///< HTTP/2 PING frames (end-to-end RTT)
  H3_PINGS,       ///< HTTP/3 PING frames (end-to-end RTT)
  
  // Cached estimates
  HTTP_CACHED_ESTIMATE,      ///< Cached HTTP RTT estimate
  TRANSPORT_CACHED_ESTIMATE, ///< Cached transport RTT estimate
  
  // Platform defaults
  DEFAULT_HTTP_FROM_PLATFORM,      ///< Platform-provided default HTTP RTT
  DEFAULT_TRANSPORT_FROM_PLATFORM  ///< Platform-provided default transport RTT
};

class Aggregator;
class ThroughputAnalyzer;
using EstimateCallback = std::function<void(const Estimate&)>;

/**
 * Network Quality Estimator (NQE)
 * 
 * Combines multiple RTT sources (HTTP TTFB, transport layer, PING) and
 * throughput measurements into quality estimates including:
 * - Combined RTT estimate
 * - Downstream throughput estimate
 * - Effective Connection Type classification
 * 
 * Thread-safe: All public methods can be called from multiple threads.
 */
class Nqe {
public:
  /// Configuration options for NQE
  struct Options {
    double decay_lambda_per_sec = 0.02;                     ///< Sample decay rate (higher = favor recent samples)
    std::chrono::milliseconds transport_sample_period{1000}; ///< TCP_INFO polling interval
    double combine_bias_to_lower = 0.6;                     ///< Bias toward lower bound [0,1]
    std::chrono::seconds freshness_threshold{60};           ///< Max age before samples are stale
    size_t min_throughput_transfer_bytes = 32000;           ///< Min bytes for throughput calculation (32KB, ~32*8*1000 bits)
    size_t throughput_min_requests_in_flight = 5;           ///< Min concurrent requests for throughput observation window
    EffectiveConnectionTypeThresholds ect_thresholds{};     ///< ECT computation thresholds
    
    // ECT recomputation options (Chromium NQE-style)
    std::chrono::seconds effective_connection_type_recomputation_interval{10}; ///< Min interval between ECT computations
    size_t count_new_observations_received_compute_ect = 50; ///< Min new observations to trigger ECT recomputation
    
    // HTTP RTT bounding and adjustment (Chrome NQE-style)
    double lower_bound_http_rtt_transport_rtt_multiplier = 1.0;  ///< HTTP RTT >= Transport RTT × this
    double lower_bound_http_rtt_end_to_end_rtt_multiplier = 0.9; ///< HTTP RTT >= End-to-End RTT × this
    double upper_bound_http_rtt_end_to_end_rtt_multiplier = 1.6; ///< HTTP RTT <= End-to-End RTT × this
    size_t http_rtt_transport_rtt_min_count = 5;                 ///< Min transport RTT samples for bounding
    size_t http_rtt_end_to_end_rtt_min_count = 5;                ///< Min end-to-end RTT samples for bounding
    bool adjust_rtt_based_on_rtt_counts = true;                  ///< Enable HTTP RTT adjustment when sample count is low
    
    // Throughput clamping (Chrome NQE-style)
    bool clamp_throughput_based_on_ect = true;                   ///< Enable throughput clamping for slow connections
    double upper_bound_typical_kbps_multiplier = 3.5;            ///< Upper bound = typical_kbps × this for slow ECT
    
    // Signal strength weighting (Chrome NQE-style)
    bool enable_signal_strength_weighting = false;               ///< Enable dual-factor weighting (time × signal)
    double weight_multiplier_per_signal_level = 0.98;            ///< Weight decay per signal strength level difference
    
    // Caching and persistence options
    bool enable_caching = false;                            ///< Enable network quality caching
    size_t max_cache_size = 100;                            ///< Maximum cached networks
    std::chrono::hours max_cache_age{24 * 7};               ///< Max age for cached data (7 days)
    std::string cache_file_path;                            ///< Path to persistent cache file (empty = no persistence)
    
    Options() = default;
  };

  explicit Nqe();
  explicit Nqe(const Options& opts);
  ~Nqe();

  // Non-copyable and non-movable
  Nqe(const Nqe&) = delete;
  Nqe& operator=(const Nqe&) = delete;
  Nqe(Nqe&&) = delete;
  Nqe& operator=(Nqe&&) = delete;

  // Core functionality
  void addSample(Source src, double rtt_ms, TimePoint ts = Clock::now());
  void addThroughputSample(size_t bytes, TimePoint start_time, TimePoint end_time);
  Estimate getEstimate(TimePoint now = Clock::now());
  EffectiveConnectionType getEffectiveConnectionType(TimePoint now = Clock::now());
  void setEstimateCallback(EstimateCallback cb);

  // Observer management
  void addRTTObserver(RTTObserver* observer);
  void removeRTTObserver(RTTObserver* observer);
  void addThroughputObserver(ThroughputObserver* observer);
  void removeThroughputObserver(ThroughputObserver* observer);
  void addEffectiveConnectionTypeObserver(EffectiveConnectionTypeObserver* observer);
  void removeEffectiveConnectionTypeObserver(EffectiveConnectionTypeObserver* observer);

  // Network change detection
  /**
   * Enable automatic network change detection
   * When enabled, NQE will monitor for network changes and optionally
   * clear estimates when the network changes (e.g., WiFi to cellular)
   * 
   * @param clear_on_change If true, clear all samples when network changes
   */
  void enableNetworkChangeDetection(bool clear_on_change = true);
  
  /**
   * Disable automatic network change detection
   */
  void disableNetworkChangeDetection();
  
  /**
   * Check if network change detection is enabled
   */
  bool isNetworkChangeDetectionEnabled() const;

  // Network quality caching
  /**
   * Get current NetworkID (if network change detection is enabled)
   * @return Current network identifier, or nullopt if unavailable
   */
  std::optional<NetworkID> getCurrentNetworkID() const;
  
  /**
   * Get current signal strength (if available)
   * @return Signal strength (0-100), or -1 if not applicable
   */
  int32_t getCurrentSignalStrength() const;

  /**
   * Update cached quality for current network
   * Called automatically when new estimates are available
   */
  void updateCachedQuality();

  /**
   * Get cached quality for a specific network
   * @param network_id Network identifier
   * @return Cached quality if available, nullopt otherwise
   */
  std::optional<Estimate> getCachedQuality(const NetworkID& network_id) const;

  /**
   * Load initial estimate from cache for current network
   * Should be called after network change to get quick initial estimate
   */
  void loadCachedQualityForCurrentNetwork();

  /**
   * Save all cached data to disk
   * @return true if saved successfully
   */
  bool saveCachedData();

  /**
   * Load cached data from disk
   * @return true if loaded successfully
   */
  bool loadCachedData();

  // Statistics and state query
  Statistics getStatistics() const;
  size_t getActiveSockets() const;
  bool isTransportSamplerRunning() const;
  
  // Configuration
  const Options& getOptions() const { return opts_; }
  
  // Validate options
  static bool validateOptions(const Options& opts, std::string* error_msg = nullptr);

#ifdef _WIN32
  using SocketHandle = uintptr_t;
#else
  using SocketHandle = int;
#endif

  void registerTcpSocket(SocketHandle sock);
  void unregisterTcpSocket(SocketHandle sock);

  void startTransportSampler();
  void stopTransportSampler();

private:
  Options opts_;
  std::unique_ptr<Aggregator> http_;
  std::unique_ptr<Aggregator> transport_;
  std::unique_ptr<Aggregator> ping_;
  std::unique_ptr<Aggregator> end_to_end_;  ///< End-to-end RTT from H2/H3 PING frames
  std::unique_ptr<ThroughputAnalyzer> throughput_;
  std::unique_ptr<NetworkQualityStore> cache_store_;

  EstimateCallback cb_;
  mutable std::recursive_mutex mu_;
  
  // Observers
  std::vector<RTTObserver*> rtt_observers_;
  std::vector<ThroughputObserver*> throughput_observers_;
  std::vector<EffectiveConnectionTypeObserver*> ect_observers_;
  EffectiveConnectionType last_ect_ = EffectiveConnectionType::UNKNOWN;

  class TransportSamplerImpl;
  std::unique_ptr<TransportSamplerImpl> sampler_;

  // Network change detection
#ifndef NQE_DISABLE_NETWORK_CHANGE_NOTIFIER
  class NetworkChangeObserverImpl;
  std::unique_ptr<NetworkChangeObserverImpl> network_observer_;
#endif
  bool network_change_detection_enabled_ = false;
  bool clear_on_network_change_ = false;
  int32_t current_signal_strength_ = -1;  ///< Current network signal strength (-1 if not applicable)

  // ECT recomputation tracking (Chromium NQE-style)
  TimePoint last_effective_connection_type_computation_{};
  size_t rtt_observations_since_last_ect_computation_ = 0;
  size_t throughput_observations_since_last_ect_computation_ = 0;
  size_t rtt_observations_at_last_ect_computation_ = 0;
  size_t throughput_observations_at_last_ect_computation_ = 0;
  bool network_change_since_last_ect_computation_ = false;

  Estimate combineLocked(TimePoint now);
  void notifyRTTObservers(double rtt_ms, const char* source);
  void notifyThroughputObservers(double throughput_kbps);
  void updateEffectiveConnectionType(EffectiveConnectionType new_ect);
  void onNetworkChange();
  void clearAllSamples();
  
  // ECT recomputation logic (Chromium NQE-style)
  bool shouldComputeEffectiveConnectionType(TimePoint now) const;
  void maybeComputeEffectiveConnectionType(TimePoint now);
  void computeEffectiveConnectionTypeLocked(TimePoint now);
  void notifyECTChangeAndUpdateCache(
      EffectiveConnectionType old_ect,
      EffectiveConnectionType new_ect,
      const std::vector<EffectiveConnectionTypeObserver*>& observers);
  
  // HTTP RTT bounding and adjustment (Chrome NQE-style)
  void updateHttpRttUsingAllRttValues(
      std::optional<double>* http_rtt,
      const std::optional<double>& transport_rtt,
      const std::optional<double>& end_to_end_rtt,
      size_t transport_rtt_count,
      size_t end_to_end_rtt_count) const;
  
  void adjustHttpRttBasedOnRTTCounts(
      std::optional<double>* http_rtt,
      size_t transport_rtt_count,
      EffectiveConnectionType effective_type) const;
  
  // Throughput clamping (Chrome NQE-style)
  void clampThroughputBasedOnEct(
      std::optional<double>* throughput_kbps,
      EffectiveConnectionType effective_type) const;
};

} // namespace nqe
