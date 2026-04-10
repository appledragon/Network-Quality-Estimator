// CachedNetworkQuality.cpp - Implementation of cached network quality
#include "nqe/CachedNetworkQuality.h"
#include <sstream>
#include <iomanip>

namespace nqe {

std::string CachedNetworkQuality::toString() const {
  if (isEmpty()) {
    return "[Empty cache]";
  }

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2);
  
  oss << "ECT:" << effectiveConnectionTypeToString(effective_type_);
  
  if (http_rtt_ms_) {
    oss << " HTTP:" << *http_rtt_ms_ << "ms";
  }
  
  if (transport_rtt_ms_) {
    oss << " Transport:" << *transport_rtt_ms_ << "ms";
  }
  
  if (downstream_throughput_kbps_) {
    oss << " Throughput:" << *downstream_throughput_kbps_ << "kbps";
  }
  
  // Show age of cached data
  auto now = std::chrono::steady_clock::now();
  auto age_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - last_update_time_).count();
  oss << " (age:" << age_seconds << "s)";
  
  return oss.str();
}

} // namespace nqe
