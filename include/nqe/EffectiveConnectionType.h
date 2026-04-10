#pragma once
#include <optional>
#include <string>

namespace nqe {

/**
 * Effective Connection Type (ECT) based on observed network quality.
 * Categories are based on Chromium's NQE implementation.
 * 
 * ECT represents the quality of the network connection, not the
 * underlying technology (Wi-Fi vs cellular). A fast Wi-Fi connection
 * will be classified as 4G, while a slow cellular connection might
 * be classified as 2G.
 */
enum class EffectiveConnectionType {
  UNKNOWN = 0,    ///< Unknown or not yet determined
  OFFLINE,        ///< No connectivity
  SLOW_2G,        ///< Very slow connection (RTT > 2000ms, throughput < 50 kbps)
  TYPE_2G,        ///< 2G-like connection (RTT 1400-2000ms, throughput 50-70 kbps)
  TYPE_3G,        ///< 3G-like connection (RTT 400-1400ms, throughput 70-700 kbps)
  TYPE_4G,        ///< 4G-like connection (RTT < 400ms, throughput > 700 kbps)
  LAST = TYPE_4G  ///< Marker for the last valid type
};

/**
 * Convert ECT to human-readable string
 */
inline const char* effectiveConnectionTypeToString(EffectiveConnectionType ect) {
  switch (ect) {
    case EffectiveConnectionType::UNKNOWN:  return "Unknown";
    case EffectiveConnectionType::OFFLINE:  return "Offline";
    case EffectiveConnectionType::SLOW_2G:  return "Slow-2G";
    case EffectiveConnectionType::TYPE_2G:  return "2G";
    case EffectiveConnectionType::TYPE_3G:  return "3G";
    case EffectiveConnectionType::TYPE_4G:  return "4G";
    default: return "Invalid";
  }
}

/**
 * Thresholds for determining Effective Connection Type.
 * Based on Chromium's NQE thresholds.
 */
struct EffectiveConnectionTypeThresholds {
  // RTT thresholds (milliseconds)
  int http_rtt_slow_2g = 2000;
  int http_rtt_2g = 1400;
  int http_rtt_3g = 400;
  
  int transport_rtt_slow_2g = 2000;
  int transport_rtt_2g = 1400;
  int transport_rtt_3g = 400;
  
  // Throughput thresholds (kilobits per second)
  int downstream_throughput_slow_2g = 50;
  int downstream_throughput_2g = 70;
  int downstream_throughput_3g = 700;
};

/**
 * Compute Effective Connection Type from network quality metrics.
 * 
 * @param http_rtt_ms HTTP RTT in milliseconds (nullopt if unknown)
 * @param transport_rtt_ms Transport RTT in milliseconds (nullopt if unknown)
 * @param downstream_throughput_kbps Downstream throughput in kbps (nullopt if unknown)
 * @param thresholds ECT thresholds to use
 * @return Computed effective connection type
 */
EffectiveConnectionType computeEffectiveConnectionType(
    std::optional<double> http_rtt_ms,
    std::optional<double> transport_rtt_ms,
    std::optional<double> downstream_throughput_kbps,
    const EffectiveConnectionTypeThresholds& thresholds = EffectiveConnectionTypeThresholds{});

} // namespace nqe
