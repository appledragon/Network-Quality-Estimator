#include "nqe/EffectiveConnectionType.h"
#include <algorithm>
#include <optional>

namespace nqe {

EffectiveConnectionType computeEffectiveConnectionType(
    std::optional<double> http_rtt_ms,
    std::optional<double> transport_rtt_ms,
    std::optional<double> downstream_throughput_kbps,
    const EffectiveConnectionTypeThresholds& thresholds) {
  
  // If no data is available, return UNKNOWN
  if (!http_rtt_ms && !transport_rtt_ms && !downstream_throughput_kbps) {
    return EffectiveConnectionType::UNKNOWN;
  }
  
  // Use the minimum of available RTT values (lower is better)
  std::optional<double> rtt_ms;
  if (http_rtt_ms && transport_rtt_ms) {
    rtt_ms = std::min(*http_rtt_ms, *transport_rtt_ms);
  } else if (http_rtt_ms) {
    rtt_ms = http_rtt_ms;
  } else if (transport_rtt_ms) {
    rtt_ms = transport_rtt_ms;
  }
  
  // Determine ECT based on RTT
  // Start pessimistic and upgrade based on better metrics
  EffectiveConnectionType ect_from_rtt = EffectiveConnectionType::TYPE_4G;
  
  if (rtt_ms) {
    if (*rtt_ms >= thresholds.http_rtt_slow_2g) {
      ect_from_rtt = EffectiveConnectionType::SLOW_2G;
    } else if (*rtt_ms >= thresholds.http_rtt_2g) {
      ect_from_rtt = EffectiveConnectionType::TYPE_2G;
    } else if (*rtt_ms >= thresholds.http_rtt_3g) {
      ect_from_rtt = EffectiveConnectionType::TYPE_3G;
    } else {
      ect_from_rtt = EffectiveConnectionType::TYPE_4G;
    }
  }
  
  // Determine ECT based on throughput
  EffectiveConnectionType ect_from_throughput = EffectiveConnectionType::TYPE_4G;
  
  if (downstream_throughput_kbps) {
    if (*downstream_throughput_kbps < thresholds.downstream_throughput_slow_2g) {
      ect_from_throughput = EffectiveConnectionType::SLOW_2G;
    } else if (*downstream_throughput_kbps < thresholds.downstream_throughput_2g) {
      ect_from_throughput = EffectiveConnectionType::TYPE_2G;
    } else if (*downstream_throughput_kbps < thresholds.downstream_throughput_3g) {
      ect_from_throughput = EffectiveConnectionType::TYPE_3G;
    } else {
      ect_from_throughput = EffectiveConnectionType::TYPE_4G;
    }
  }
  
  // Return the worse (more conservative) of the two estimates
  if (!rtt_ms) {
    return ect_from_throughput;
  }
  if (!downstream_throughput_kbps) {
    return ect_from_rtt;
  }
  
  // Both available - return the worse connection type
  // Note: Lower enum values = worse connection (SLOW_2G=2 < TYPE_4G=5)
  return std::min(ect_from_rtt, ect_from_throughput);
}

} // namespace nqe
