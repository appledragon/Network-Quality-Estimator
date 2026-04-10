#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

// Forward declarations for JNI friend functions (Android only)
#ifdef __ANDROID__
#include <jni.h>
extern "C" {
  JNIEXPORT void JNICALL Java_com_nqe_NetworkChangeNotifier_nativeInit(JNIEnv*, jclass, jobject);
  JNIEXPORT void JNICALL Java_com_nqe_NetworkChangeNotifier_nativeCleanup(JNIEnv*, jclass);
  JNIEXPORT void JNICALL Java_com_nqe_NetworkChangeNotifier_nativeOnNetworkChanged(JNIEnv*, jclass, jint);
}
#endif

namespace nqe {

/**
 * Type of network connection
 * Mirrors Chromium's ConnectionType enum
 */
enum class ConnectionType {
  UNKNOWN = 0,      ///< Connection type is unknown or cannot be determined
  ETHERNET = 1,     ///< Wired Ethernet connection
  WIFI = 2,         ///< WiFi/802.11 connection
  CELLULAR_2G = 3,  ///< 2G cellular connection
  CELLULAR_3G = 4,  ///< 3G cellular connection
  CELLULAR_4G = 5,  ///< 4G/LTE cellular connection
  CELLULAR_5G = 6,  ///< 5G cellular connection
  BLUETOOTH = 7,    ///< Bluetooth connection
  NONE = 8          ///< No network connection
};

/**
 * Converts ConnectionType to human-readable string
 */
const char* connectionTypeToString(ConnectionType type);

/**
 * Observer interface for network change notifications
 * Similar to Chromium's NetworkChangeNotifier::NetworkChangeObserver
 */
class NetworkChangeObserver {
public:
  virtual ~NetworkChangeObserver() = default;
  
  /**
   * Called when the network connection type changes
   * @param type New connection type
   */
  virtual void onNetworkChanged(ConnectionType type) = 0;
};

/**
 * Cross-platform network change detection
 * Inspired by Chromium's net/base/network_change_notifier.h
 * 
 * Detects changes in network connectivity and notifies registered observers.
 * Supports platform-specific detection mechanisms:
 * - Android: netlink sockets + JNI callbacks for detailed network info
 * - Linux: netlink sockets (RTMGRP_LINK)
 * - Windows: NotifyAddrChange/NotifyRouteChange
 * - macOS: SystemConfiguration framework
 * 
 * Android JNI Integration:
 * - Call Java_com_nqe_NetworkChangeNotifier_nativeInit() from Java to set up JNI
 * - Implement getConnectionType() method in Java callback to return network type
 * - Call Java_com_nqe_NetworkChangeNotifier_nativeOnNetworkChanged() from Java when network changes
 * 
 * Thread-safe: All public methods can be called from multiple threads.
 */
class NetworkChangeNotifier {
public:
  /**
   * Get the singleton instance
   * 
   * Note: Unlike Chromium which uses a global singleton pattern with
   * Create()/Destroy(), this implementation uses a simpler approach
   * where the instance is created on first use.
   */
  static NetworkChangeNotifier& instance();

  /**
   * Prevent copying and assignment
   */
  NetworkChangeNotifier(const NetworkChangeNotifier&) = delete;
  NetworkChangeNotifier& operator=(const NetworkChangeNotifier&) = delete;

  /**
   * Start monitoring network changes
   * Spawns a background thread for platform-specific monitoring
   */
  void start();

  /**
   * Stop monitoring network changes
   * Stops the background monitoring thread
   */
  void stop();

  /**
   * Explicitly shut down the singleton before static destruction.
   * Call this during application cleanup to avoid dangling references
   * from other static/global destructors referencing the singleton.
   */
  void shutdown();

  /**
   * Check if monitoring is active
   */
  bool isMonitoring() const;

  /**
   * Get current connection type
   * This may perform a synchronous check of the network state
   */
  ConnectionType getCurrentConnectionType();

  /**
   * Register an observer for network change notifications
   * Observer must remain valid until removed
   * 
   * @param observer Pointer to observer (must not be nullptr)
   */
  void addObserver(NetworkChangeObserver* observer);

  /**
   * Unregister an observer
   * 
   * @param observer Pointer to observer to remove
   */
  void removeObserver(NetworkChangeObserver* observer);

  /**
   * Manually trigger a network change check
   * Useful for testing or forcing a refresh
   */
  void checkForChanges();

  ~NetworkChangeNotifier();

private:
  NetworkChangeNotifier();

  // Platform-specific implementation
  class PlatformImpl;
  std::unique_ptr<PlatformImpl> impl_;

  // Observer management
  mutable std::mutex observers_mutex_;
  std::vector<NetworkChangeObserver*> observers_;
  ConnectionType last_connection_type_;

  // Notify all observers of a connection type change
  void notifyObservers(ConnectionType new_type);

  // Called by platform implementation when network changes
  void onConnectionTypeChanged(ConnectionType new_type);

  friend class PlatformImpl;
  
#ifdef __ANDROID__
  // Friend declarations for JNI functions
  friend void ::Java_com_nqe_NetworkChangeNotifier_nativeInit(JNIEnv*, jclass, jobject);
  friend void ::Java_com_nqe_NetworkChangeNotifier_nativeCleanup(JNIEnv*, jclass);
  friend void ::Java_com_nqe_NetworkChangeNotifier_nativeOnNetworkChanged(JNIEnv*, jclass, jint);
#endif
};

} // namespace nqe
