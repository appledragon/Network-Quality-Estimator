// NetworkID.h - Network identification for quality caching
// Inspired by Chromium's net/nqe/network_id.h
#pragma once

#include <string>
#include <functional>

namespace nqe {

/**
 * NetworkID uniquely identifies a network for caching network quality estimates.
 * 
 * This follows Chromium's approach of identifying networks by:
 * - Connection type (WiFi, Cellular, Ethernet, etc.)
 * - Network name (SSID for WiFi, carrier name for cellular)
 * - Signal strength (optional, for wireless networks)
 * 
 * NetworkID is used as a key to cache and retrieve network quality estimates,
 * allowing the system to "remember" the quality of previously connected networks
 * and provide faster initial estimates when reconnecting.
 */
class NetworkID {
public:
  enum class ConnectionType {
    UNKNOWN = 0,
    ETHERNET,
    WIFI,
    CELLULAR_2G,
    CELLULAR_3G,
    CELLULAR_4G,
    CELLULAR_5G,
    BLUETOOTH,
    NONE
  };

  /**
   * Default constructor - creates an UNKNOWN network ID
   */
  NetworkID() : type_(ConnectionType::UNKNOWN), name_(""), signal_strength_(-1) {}

  /**
   * Constructor with connection type and network name
   * @param type Connection type (WiFi, Cellular, etc.)
   * @param name Network name (SSID for WiFi, carrier for cellular, empty for wired)
   */
  NetworkID(ConnectionType type, const std::string& name)
    : type_(type), name_(name), signal_strength_(-1) {}

  /**
   * Constructor with connection type, network name, and signal strength
   * @param type Connection type
   * @param name Network name
   * @param signal_strength Signal strength (0-100), -1 if not applicable
   */
  NetworkID(ConnectionType type, const std::string& name, int signal_strength)
    : type_(type), name_(name), signal_strength_(signal_strength) {}

  // Getters
  ConnectionType type() const { return type_; }
  const std::string& name() const { return name_; }
  int signal_strength() const { return signal_strength_; }

  /**
   * Check if this is a valid network ID (not UNKNOWN/NONE)
   */
  bool isValid() const {
    return type_ != ConnectionType::UNKNOWN && type_ != ConnectionType::NONE;
  }

  /**
   * Equality comparison (for use as map key)
   * Two NetworkIDs are equal if they have the same type and name.
   * Signal strength is not used in equality comparison.
   */
  bool operator==(const NetworkID& other) const {
    return type_ == other.type_ && name_ == other.name_;
  }

  bool operator!=(const NetworkID& other) const {
    return !(*this == other);
  }

  /**
   * Less-than comparison (for use in ordered containers)
   */
  bool operator<(const NetworkID& other) const {
    if (type_ != other.type_) {
      return static_cast<int>(type_) < static_cast<int>(other.type_);
    }
    return name_ < other.name_;
  }

  /**
   * Convert to string for debugging/logging
   */
  std::string toString() const;

  /**
   * Convert ConnectionType to string
   */
  static std::string connectionTypeToString(ConnectionType type);

private:
  ConnectionType type_;
  std::string name_;         ///< SSID for WiFi, carrier for cellular, empty for wired
  int signal_strength_;      ///< Signal strength 0-100, -1 if not applicable
};

} // namespace nqe

// Hash function for NetworkID (for use in unordered_map)
namespace std {
  template<>
  struct hash<nqe::NetworkID> {
    size_t operator()(const nqe::NetworkID& id) const {
      // Combine hash of type and name
      size_t h1 = std::hash<int>{}(static_cast<int>(id.type()));
      size_t h2 = std::hash<std::string>{}(id.name());
      return h1 ^ (h2 << 1);
    }
  };
}
