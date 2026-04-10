// NetworkID.cpp - Implementation of network identification
#include "nqe/NetworkID.h"
#include <sstream>

namespace nqe {

std::string NetworkID::toString() const {
  std::ostringstream oss;
  oss << connectionTypeToString(type_);
  
  if (!name_.empty()) {
    oss << ":" << name_;
  }
  
  if (signal_strength_ >= 0) {
    oss << " (signal:" << signal_strength_ << ")";
  }
  
  return oss.str();
}

std::string NetworkID::connectionTypeToString(ConnectionType type) {
  switch (type) {
    case ConnectionType::ETHERNET:
      return "Ethernet";
    case ConnectionType::WIFI:
      return "WiFi";
    case ConnectionType::CELLULAR_2G:
      return "Cellular-2G";
    case ConnectionType::CELLULAR_3G:
      return "Cellular-3G";
    case ConnectionType::CELLULAR_4G:
      return "Cellular-4G";
    case ConnectionType::CELLULAR_5G:
      return "Cellular-5G";
    case ConnectionType::BLUETOOTH:
      return "Bluetooth";
    case ConnectionType::NONE:
      return "None";
    case ConnectionType::UNKNOWN:
    default:
      return "Unknown";
  }
}

} // namespace nqe
