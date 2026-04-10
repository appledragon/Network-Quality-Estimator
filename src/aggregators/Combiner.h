#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace nqe {

// combineRtt - Combines RTT estimates from different sources
inline std::optional<double> combineRtt(
    std::optional<double> ping_lower_ms,
    std::optional<double> transport_lower_ms,
    std::optional<double> http_upper_ms,
    double bias_to_lower /*0..1*/) {

  std::optional<double> lower;
  if (ping_lower_ms && transport_lower_ms) lower = std::min(*ping_lower_ms, *transport_lower_ms);
  else lower = ping_lower_ms ? ping_lower_ms : transport_lower_ms;

  if (!lower && http_upper_ms) return http_upper_ms;
  if (lower && !http_upper_ms) return lower;

  if (lower && http_upper_ms) {
    double lo = *lower;
    double hi = *http_upper_ms;
    if (hi < lo) {
      double g = std::exp((std::log(std::max(1e-6, lo)) + std::log(std::max(1e-6, hi)))/2.0);
      return g;
    }
    double t = std::clamp(bias_to_lower, 0.0, 1.0);
    double y = std::exp((1.0 - t) * std::log(hi) + t * std::log(lo));
    y = std::clamp(y, lo, hi);
    return y;
  }

  return std::nullopt;
}

} // namespace nqe
