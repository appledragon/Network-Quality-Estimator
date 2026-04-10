// CachedNetworkQuality.h - Cached network quality estimates per NetworkID
// Inspired by Chromium's net/nqe/cached_network_quality.h
#pragma once

#include "nqe/EffectiveConnectionType.h"
#include <chrono>
#include <optional>

namespace nqe {

/**
 * CachedNetworkQuality stores network quality estimates for a specific network.
 * 
 * This follows Chromium's approach of caching:
 * - RTT estimates (HTTP and Transport)
 * - Downstream throughput estimate
 * - Effective Connection Type
 * - Timestamp of last update
 * 
 * Cached values are used to provide quick initial estimates when reconnecting
 * to a known network, before fresh samples are collected.
 */
class CachedNetworkQuality {
public:
  using TimePoint = std::chrono::steady_clock::time_point;

  /**
   * Default constructor - creates empty cache entry
   */
  CachedNetworkQuality()
    : http_rtt_ms_(),
      transport_rtt_ms_(),
      downstream_throughput_kbps_(),
      effective_type_(EffectiveConnectionType::UNKNOWN),
      last_update_time_() {}

  /**
   * Constructor with all quality metrics
   */
  CachedNetworkQuality(
    std::optional<double> http_rtt_ms,
    std::optional<double> transport_rtt_ms,
    std::optional<double> downstream_throughput_kbps,
    EffectiveConnectionType effective_type,
    TimePoint last_update_time = std::chrono::steady_clock::now())
    : http_rtt_ms_(http_rtt_ms),
      transport_rtt_ms_(transport_rtt_ms),
      downstream_throughput_kbps_(downstream_throughput_kbps),
      effective_type_(effective_type),
      last_update_time_(last_update_time) {}

  // Getters
  std::optional<double> http_rtt_ms() const { return http_rtt_ms_; }
  std::optional<double> transport_rtt_ms() const { return transport_rtt_ms_; }
  std::optional<double> downstream_throughput_kbps() const { return downstream_throughput_kbps_; }
  EffectiveConnectionType effective_type() const { return effective_type_; }
  TimePoint last_update_time() const { return last_update_time_; }

  /**
   * Check if this cache entry has any valid data
   */
  bool isEmpty() const {
    return !http_rtt_ms_.has_value() && 
           !transport_rtt_ms_.has_value() && 
           !downstream_throughput_kbps_.has_value() &&
           effective_type_ == EffectiveConnectionType::UNKNOWN;
  }

  /**
   * Check if this cache entry is recent enough to be useful
   * @param max_age Maximum age for cached data to be considered valid
   * @param now Current time
   * @return true if cache entry is fresh, false if stale
   */
  bool isFresh(std::chrono::seconds max_age, TimePoint now = std::chrono::steady_clock::now()) const {
    if (isEmpty()) return false;
    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - last_update_time_);
    return age <= max_age;
  }

  /**
   * Update the cached quality metrics
   */
  void update(
    std::optional<double> http_rtt_ms,
    std::optional<double> transport_rtt_ms,
    std::optional<double> downstream_throughput_kbps,
    EffectiveConnectionType effective_type,
    TimePoint update_time = std::chrono::steady_clock::now()) {
    
    http_rtt_ms_ = http_rtt_ms;
    transport_rtt_ms_ = transport_rtt_ms;
    downstream_throughput_kbps_ = downstream_throughput_kbps;
    effective_type_ = effective_type;
    last_update_time_ = update_time;
  }

  /**
   * Convert to string for debugging/logging
   */
  std::string toString() const;

private:
  std::optional<double> http_rtt_ms_;
  std::optional<double> transport_rtt_ms_;
  std::optional<double> downstream_throughput_kbps_;
  EffectiveConnectionType effective_type_;
  TimePoint last_update_time_;
};

} // namespace nqe
