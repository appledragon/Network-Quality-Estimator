#include "nqe/NetworkChangeNotifier.h"
#include "nqe/Logger.h"
#include <atomic>
#include <thread>
#include <cstring>

#ifdef __ANDROID__
#include <jni.h>
#include <android/log.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <unistd.h>
#include <net/if.h>
#include <ifaddrs.h>
#elif defined(__linux__)
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <unistd.h>
#include <net/if.h>
#include <ifaddrs.h>
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#elif defined(__APPLE__)
#include <SystemConfiguration/SystemConfiguration.h>
#include <netinet/in.h>
#include <net/if.h>
#include <ifaddrs.h>
#endif

namespace nqe {

const char* connectionTypeToString(ConnectionType type) {
  switch (type) {
    case ConnectionType::UNKNOWN: return "Unknown";
    case ConnectionType::ETHERNET: return "Ethernet";
    case ConnectionType::WIFI: return "WiFi";
    case ConnectionType::CELLULAR_2G: return "2G";
    case ConnectionType::CELLULAR_3G: return "3G";
    case ConnectionType::CELLULAR_4G: return "4G";
    case ConnectionType::CELLULAR_5G: return "5G";
    case ConnectionType::BLUETOOTH: return "Bluetooth";
    case ConnectionType::NONE: return "None";
    default: return "Unknown";
  }
}

// Platform-specific implementation
class NetworkChangeNotifier::PlatformImpl {
public:
  PlatformImpl(NetworkChangeNotifier* parent) 
    : parent_(parent), monitoring_(false), should_stop_(false) {}

  ~PlatformImpl() {
    stop();
  }

  void start() {
    if (monitoring_.exchange(true)) {
      return; // Already monitoring
    }

    should_stop_ = false;
    monitor_thread_ = std::thread(&PlatformImpl::monitorLoop, this);
    
    Logger::instance().log(LogLevel::LOG_INFO, "NetworkChangeNotifier: Started monitoring");
  }

  void stop() {
    if (!monitoring_.exchange(false)) {
      return; // Not monitoring
    }

    should_stop_ = true;

#ifdef __APPLE__
    // Stop the run loop
    if (run_loop_ != nullptr) {
      CFRunLoopStop(run_loop_);
    }
#elif defined(_WIN32)
    // Cancel the notification on Windows
    if (notify_handle_ != nullptr) {
      CancelIPChangeNotify(&overlap_);
      notify_handle_ = nullptr;
    }
#endif

    if (monitor_thread_.joinable()) {
      monitor_thread_.join();
    }

    Logger::instance().log(LogLevel::LOG_INFO, "NetworkChangeNotifier: Stopped monitoring");
  }

  bool isMonitoring() const {
    return monitoring_;
  }

  ConnectionType getCurrentConnectionType() {
#ifdef __ANDROID__
    return getConnectionTypeAndroid();
#elif defined(__linux__)
    return getConnectionTypeLinux();
#elif defined(_WIN32)
    return getConnectionTypeWindows();
#elif defined(__APPLE__)
    return getConnectionTypeMacOS();
#else
    return ConnectionType::UNKNOWN;
#endif
  }

private:
  NetworkChangeNotifier* parent_;
  std::atomic<bool> monitoring_;
  std::atomic<bool> should_stop_;
  std::thread monitor_thread_;

#ifdef __ANDROID__
  int netlink_fd_ = -1;
  
  // JNI global references - must be set from Java layer
  // Made public for JNI access
public:
  static JavaVM* java_vm_;
  static jobject network_callback_obj_;
  
private:
  // JNI helper to get current network type from Android
  static ConnectionType getNetworkTypeFromJava() {
    if (!java_vm_ || !network_callback_obj_) {
      // JNI not configured - will fall back to native interface detection
      return ConnectionType::UNKNOWN;
    }
    
    JNIEnv* env = nullptr;
    bool attached = false;
    
    // Get JNI environment
    int status = java_vm_->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
      if (java_vm_->AttachCurrentThread(&env, nullptr) == JNI_OK) {
        attached = true;
      } else {
        return ConnectionType::UNKNOWN;
      }
    } else if (status != JNI_OK) {
      return ConnectionType::UNKNOWN;
    }
    
    ConnectionType result = ConnectionType::UNKNOWN;
    
    // Call Java method to get network type
    jclass cls = env->GetObjectClass(network_callback_obj_);
    if (cls) {
      jmethodID mid = env->GetMethodID(cls, "getConnectionType", "()I");
      if (mid) {
        jint type = env->CallIntMethod(network_callback_obj_, mid);
        result = static_cast<ConnectionType>(type);
      }
      env->DeleteLocalRef(cls);
    }
    
    if (attached) {
      java_vm_->DetachCurrentThread();
    }
    
    return result;
  }
  
  void monitorLoop() {
    // On Android, we use netlink for basic monitoring like Linux
    // but also support JNI callbacks for more detailed info
    netlink_fd_ = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (netlink_fd_ < 0) {
      Logger::instance().log(LogLevel::LOG_ERROR, 
        "NetworkChangeNotifier: Failed to create netlink socket on Android");
      monitoring_ = false;
      return;
    }

    struct sockaddr_nl addr{};
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;

    if (bind(netlink_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      Logger::instance().log(LogLevel::LOG_ERROR,
        "NetworkChangeNotifier: Failed to bind netlink socket on Android");
      close(netlink_fd_);
      netlink_fd_ = -1;
      monitoring_ = false;
      return;
    }

    Logger::instance().log(LogLevel::LOG_DEBUG,
      "NetworkChangeNotifier: Android netlink monitor started");

    char buffer[4096];
    while (!should_stop_) {
      fd_set readfds;
      FD_ZERO(&readfds);
      FD_SET(netlink_fd_, &readfds);
      
      struct timeval timeout;
      timeout.tv_sec = 1;
      timeout.tv_usec = 0;
      
      int ret = select(netlink_fd_ + 1, &readfds, nullptr, nullptr, &timeout);
      if (ret < 0) {
        if (!should_stop_) {
          Logger::instance().log(LogLevel::LOG_WARNING,
            "NetworkChangeNotifier: select() error on Android");
        }
        break;
      } else if (ret == 0) {
        continue;
      }
      
      ssize_t len = recv(netlink_fd_, buffer, sizeof(buffer), 0);
      if (len < 0) {
        if (!should_stop_) {
          Logger::instance().log(LogLevel::LOG_WARNING,
            "NetworkChangeNotifier: Error reading netlink socket on Android");
        }
        break;
      }

      struct nlmsghdr* nh = (struct nlmsghdr*)buffer;
      for (; NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len)) {
        if (nh->nlmsg_type == RTM_NEWLINK || nh->nlmsg_type == RTM_DELLINK ||
            nh->nlmsg_type == RTM_NEWADDR || nh->nlmsg_type == RTM_DELADDR) {
          
          Logger::instance().log(LogLevel::LOG_DEBUG,
            "NetworkChangeNotifier: Detected network interface change on Android");
          
          ConnectionType new_type = getCurrentConnectionType();
          parent_->onConnectionTypeChanged(new_type);
        }
      }
    }

    if (netlink_fd_ >= 0) {
      close(netlink_fd_);
      netlink_fd_ = -1;
    }
  }

  ConnectionType getConnectionTypeAndroid() {
    // Try JNI first for accurate Android network type
    if (java_vm_ && network_callback_obj_) {
      ConnectionType jni_type = getNetworkTypeFromJava();
      if (jni_type != ConnectionType::UNKNOWN) {
        return jni_type;
      }
    }
    
    // Fallback to basic interface detection
    // Use getifaddrs if available (API 24+), otherwise use ioctl
    ConnectionType result = ConnectionType::NONE;
    
#if __ANDROID_API__ >= 24
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
      return ConnectionType::UNKNOWN;
    }
    
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == nullptr) continue;
      if (ifa->ifa_flags & IFF_LOOPBACK) continue;
      if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_RUNNING)) continue;

      const char* name = ifa->ifa_name;
      
      // Android interface naming patterns
      if (strncmp(name, "wlan", 4) == 0) {
        result = ConnectionType::WIFI;
      } else if (strncmp(name, "rmnet", 5) == 0 || strncmp(name, "ccmni", 5) == 0) {
        // rmnet is common for cellular on Qualcomm, ccmni on MediaTek
        result = ConnectionType::CELLULAR_4G; // Default to 4G
      } else if (strncmp(name, "eth", 3) == 0) {
        result = ConnectionType::ETHERNET;
      } else if (strncmp(name, "bt-pan", 6) == 0) {
        result = ConnectionType::BLUETOOTH;
      }
      
      if (result != ConnectionType::NONE) {
        break;
      }
    }

    freeifaddrs(ifaddr);
#else
    // For older Android versions, just return UNKNOWN
    // The JNI layer should provide the accurate network type
    result = ConnectionType::UNKNOWN;
#endif
    return result;
  }

#elif defined(__linux__)
  int netlink_fd_ = -1;

  void monitorLoop() {
    // Create netlink socket to monitor network interface changes
    netlink_fd_ = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (netlink_fd_ < 0) {
      Logger::instance().log(LogLevel::LOG_ERROR, 
        "NetworkChangeNotifier: Failed to create netlink socket");
      monitoring_ = false;
      return;
    }

    struct sockaddr_nl addr{};
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;

    if (bind(netlink_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      Logger::instance().log(LogLevel::LOG_ERROR,
        "NetworkChangeNotifier: Failed to bind netlink socket");
      close(netlink_fd_);
      netlink_fd_ = -1;
      monitoring_ = false;
      return;
    }

    Logger::instance().log(LogLevel::LOG_DEBUG,
      "NetworkChangeNotifier: Linux netlink monitor started");

    char buffer[4096];
    while (!should_stop_) {
      // Use select to wait with timeout so we can check should_stop_
      fd_set readfds;
      FD_ZERO(&readfds);
      FD_SET(netlink_fd_, &readfds);
      
      struct timeval timeout;
      timeout.tv_sec = 1;  // 1 second timeout
      timeout.tv_usec = 0;
      
      int ret = select(netlink_fd_ + 1, &readfds, nullptr, nullptr, &timeout);
      if (ret < 0) {
        if (!should_stop_) {
          Logger::instance().log(LogLevel::LOG_WARNING,
            "NetworkChangeNotifier: select() error");
        }
        break;
      } else if (ret == 0) {
        // Timeout - loop back to check should_stop_
        continue;
      }
      
      // Data available, read it
      ssize_t len = recv(netlink_fd_, buffer, sizeof(buffer), 0);
      if (len < 0) {
        if (!should_stop_) {
          Logger::instance().log(LogLevel::LOG_WARNING,
            "NetworkChangeNotifier: Error reading netlink socket");
        }
        break;
      }

      // Parse netlink messages
      struct nlmsghdr* nh = (struct nlmsghdr*)buffer;
      for (; NLMSG_OK(nh, len); nh = NLMSG_NEXT(nh, len)) {
        if (nh->nlmsg_type == RTM_NEWLINK || nh->nlmsg_type == RTM_DELLINK ||
            nh->nlmsg_type == RTM_NEWADDR || nh->nlmsg_type == RTM_DELADDR) {
          
          Logger::instance().log(LogLevel::LOG_DEBUG,
            "NetworkChangeNotifier: Detected network interface change");
          
          // Get new connection type and notify
          ConnectionType new_type = getCurrentConnectionType();
          parent_->onConnectionTypeChanged(new_type);
        }
      }
    }

    if (netlink_fd_ >= 0) {
      close(netlink_fd_);
      netlink_fd_ = -1;
    }
  }

  ConnectionType getConnectionTypeLinux() {
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
      return ConnectionType::UNKNOWN;
    }

    ConnectionType result = ConnectionType::NONE;
    
    // Look for active interfaces
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == nullptr) continue;
      
      // Skip loopback
      if (ifa->ifa_flags & IFF_LOOPBACK) continue;
      
      // Check if interface is up and running
      if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_RUNNING)) continue;

      const char* name = ifa->ifa_name;
      
      // Heuristic detection based on interface name
      if (strncmp(name, "eth", 3) == 0 || strncmp(name, "en", 2) == 0) {
        // Could be ethernet or wifi, assume ethernet if not wireless
        // In a real implementation, we'd check wireless extensions
        result = ConnectionType::ETHERNET;
      } else if (strncmp(name, "wlan", 4) == 0 || strncmp(name, "wl", 2) == 0) {
        result = ConnectionType::WIFI;
      } else if (strncmp(name, "ww", 2) == 0 || strncmp(name, "ppp", 3) == 0) {
        result = ConnectionType::CELLULAR_4G; // Assume 4G for cellular
      }
      
      // If we found an active connection, we can stop
      if (result != ConnectionType::NONE) {
        break;
      }
    }

    freeifaddrs(ifaddr);
    return result;
  }

#elif defined(_WIN32)
  HANDLE notify_handle_ = nullptr;
  OVERLAPPED overlap_{};

  void monitorLoop() {
    Logger::instance().log(LogLevel::LOG_DEBUG,
      "NetworkChangeNotifier: Windows monitor started");

    // Create event handle for overlapped I/O notification
    overlap_.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (overlap_.hEvent == nullptr) {
      Logger::instance().log(LogLevel::LOG_ERROR,
        "NetworkChangeNotifier: Failed to create event handle");
      monitoring_ = false;
      return;
    }

    while (!should_stop_) {
      // Use NotifyAddrChange to detect network changes
      DWORD result = NotifyAddrChange(&notify_handle_, &overlap_);
      
      if (result != NO_ERROR && result != ERROR_IO_PENDING) {
        Logger::instance().log(LogLevel::LOG_ERROR,
          "NetworkChangeNotifier: NotifyAddrChange failed");
        break;
      }

      // Wait for notification or stop signal
      if (result == ERROR_IO_PENDING) {
        // Wait for the notification (with timeout to check should_stop_)
        DWORD wait_result = WaitForSingleObject(overlap_.hEvent, 1000);
        
        if (should_stop_) break;
        
        if (wait_result == WAIT_OBJECT_0) {
          Logger::instance().log(LogLevel::LOG_DEBUG,
            "NetworkChangeNotifier: Detected network address change");
          
          ConnectionType new_type = getCurrentConnectionType();
          parent_->onConnectionTypeChanged(new_type);
        }
      } else {
        // Immediate notification
        Logger::instance().log(LogLevel::LOG_DEBUG,
          "NetworkChangeNotifier: Detected network address change");
        
        ConnectionType new_type = getCurrentConnectionType();
        parent_->onConnectionTypeChanged(new_type);
      }
    }

    // Clean up the event handle
    if (overlap_.hEvent != nullptr) {
      CloseHandle(overlap_.hEvent);
      overlap_.hEvent = nullptr;
    }
  }

  ConnectionType getConnectionTypeWindows() {
    // Get adapter information
    ULONG size = 0;
    if (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, nullptr, &size) != ERROR_BUFFER_OVERFLOW) {
      return ConnectionType::UNKNOWN;
    }

    std::vector<char> buffer(size);
    PIP_ADAPTER_ADDRESSES adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
    
    if (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, adapters, &size) != NO_ERROR) {
      return ConnectionType::UNKNOWN;
    }

    // Find the first operational adapter
    for (PIP_ADAPTER_ADDRESSES adapter = adapters; adapter; adapter = adapter->Next) {
      if (adapter->OperStatus != IfOperStatusUp) continue;
      
      if (adapter->IfType == IF_TYPE_ETHERNET_CSMACD) {
        return ConnectionType::ETHERNET;
      } else if (adapter->IfType == IF_TYPE_IEEE80211) {
        return ConnectionType::WIFI;
      } else if (adapter->IfType == IF_TYPE_WWANPP || adapter->IfType == IF_TYPE_WWANPP2) {
        return ConnectionType::CELLULAR_4G;
      }
    }

    return ConnectionType::NONE;
  }

#elif defined(__APPLE__)
  CFRunLoopRef run_loop_ = nullptr;
  SCDynamicStoreRef store_ = nullptr;

  static void networkChangeCallback(SCDynamicStoreRef store, CFArrayRef changedKeys, void* info) {
    PlatformImpl* impl = static_cast<PlatformImpl*>(info);
    
    Logger::instance().log(LogLevel::LOG_DEBUG,
      "NetworkChangeNotifier: Detected network configuration change (macOS)");
    
    ConnectionType new_type = impl->getCurrentConnectionType();
    impl->parent_->onConnectionTypeChanged(new_type);
  }

  void monitorLoop() {
    Logger::instance().log(LogLevel::LOG_DEBUG,
      "NetworkChangeNotifier: macOS SystemConfiguration monitor started");

    // Create dynamic store
    SCDynamicStoreContext context = {0, this, nullptr, nullptr, nullptr};
    store_ = SCDynamicStoreCreate(nullptr, CFSTR("NetworkChangeNotifier"), 
                                   networkChangeCallback, &context);
    
    if (!store_) {
      Logger::instance().log(LogLevel::LOG_ERROR,
        "NetworkChangeNotifier: Failed to create SCDynamicStore");
      monitoring_ = false;
      return;
    }

    // Set up notification for network changes
    CFStringRef patterns[] = {
      CFSTR("State:/Network/Global/IPv4"),
      CFSTR("State:/Network/Global/IPv6"),
      CFSTR("State:/Network/Interface/.*/Link")
    };
    CFArrayRef patternsArray = CFArrayCreate(nullptr, 
      (const void**)patterns, 3, &kCFTypeArrayCallBacks);

    if (!SCDynamicStoreSetNotificationKeys(store_, nullptr, patternsArray)) {
      Logger::instance().log(LogLevel::LOG_ERROR,
        "NetworkChangeNotifier: Failed to set notification keys");
      CFRelease(patternsArray);
      CFRelease(store_);
      store_ = nullptr;
      monitoring_ = false;
      return;
    }
    CFRelease(patternsArray);

    // Create run loop source and add to run loop
    CFRunLoopSourceRef runLoopSource = SCDynamicStoreCreateRunLoopSource(nullptr, store_, 0);
    if (!runLoopSource) {
      Logger::instance().log(LogLevel::LOG_ERROR,
        "NetworkChangeNotifier: Failed to create run loop source");
      CFRelease(store_);
      store_ = nullptr;
      monitoring_ = false;
      return;
    }

    run_loop_ = CFRunLoopGetCurrent();
    CFRunLoopAddSource(run_loop_, runLoopSource, kCFRunLoopDefaultMode);
    CFRelease(runLoopSource);

    // Run the loop until stopped
    while (!should_stop_) {
      CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, true);
    }

    CFRelease(store_);
    store_ = nullptr;
    run_loop_ = nullptr;
  }

  ConnectionType getConnectionTypeMacOS() {
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
      return ConnectionType::UNKNOWN;
    }

    ConnectionType result = ConnectionType::NONE;
    
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == nullptr) continue;
      if (ifa->ifa_flags & IFF_LOOPBACK) continue;
      if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_RUNNING)) continue;

      const char* name = ifa->ifa_name;
      
      // macOS interface naming conventions:
      // en0 = built-in WiFi on most Macs (or Ethernet on Mac Pro/iMac without WiFi)
      // en1 = secondary network interface (often Thunderbolt Ethernet)
      // awdl0 = Apple Wireless Direct Link (AirDrop, not user network)
      // llw0 = Low Latency WLAN (companion link)
      // pdp_ip = cellular (iPad/iPhone)
      // bridge = bridged interface (e.g., Internet Sharing)
      if (strncmp(name, "en0", 3) == 0) {
        // en0 is typically WiFi on most Mac models
        result = ConnectionType::WIFI;
      } else if (strncmp(name, "en", 2) == 0) {
        // Other enX interfaces are typically Ethernet (Thunderbolt, USB, etc.)
        // Only set to Ethernet if we haven't found WiFi yet
        if (result == ConnectionType::NONE) {
          result = ConnectionType::ETHERNET;
        }
      } else if (strncmp(name, "pdp_ip", 6) == 0) {
        result = ConnectionType::CELLULAR_4G;
      }
      
      // WiFi takes priority; keep looking if we only found Ethernet
      if (result == ConnectionType::WIFI || result == ConnectionType::CELLULAR_4G) {
        break;
      }
    }

    freeifaddrs(ifaddr);
    return result;
  }
#else
  void monitorLoop() {
    Logger::instance().log(LogLevel::LOG_WARNING,
      "NetworkChangeNotifier: Unsupported platform, monitoring disabled");
  }

  ConnectionType getCurrentConnectionType() {
    return ConnectionType::UNKNOWN;
  }
#endif
};

// NetworkChangeNotifier implementation

NetworkChangeNotifier& NetworkChangeNotifier::instance() {
  static NetworkChangeNotifier instance;
  return instance;
}

NetworkChangeNotifier::NetworkChangeNotifier() 
  : impl_(std::make_unique<PlatformImpl>(this))
  , last_connection_type_(ConnectionType::UNKNOWN) {
}

NetworkChangeNotifier::~NetworkChangeNotifier() {
  stop();
}

void NetworkChangeNotifier::start() {
  impl_->start();
  
  // Get initial connection type
  last_connection_type_ = getCurrentConnectionType();
  Logger::instance().log(LogLevel::LOG_INFO,
    std::string("NetworkChangeNotifier: Initial connection type: ") +
    connectionTypeToString(last_connection_type_));
}

void NetworkChangeNotifier::stop() {
  impl_->stop();
}

void NetworkChangeNotifier::shutdown() {
  stop();
  
  // Clear all observers to prevent dangling references during static destruction
  std::lock_guard<std::mutex> lock(observers_mutex_);
  observers_.clear();
  
  Logger::instance().log(LogLevel::LOG_INFO,
    "NetworkChangeNotifier: Shutdown complete");
}

bool NetworkChangeNotifier::isMonitoring() const {
  return impl_->isMonitoring();
}

ConnectionType NetworkChangeNotifier::getCurrentConnectionType() {
  return impl_->getCurrentConnectionType();
}

void NetworkChangeNotifier::addObserver(NetworkChangeObserver* observer) {
  if (!observer) return;
  
  std::lock_guard<std::mutex> lock(observers_mutex_);
  observers_.push_back(observer);
  
  Logger::instance().log(LogLevel::LOG_DEBUG,
    "NetworkChangeNotifier: Observer added");
}

void NetworkChangeNotifier::removeObserver(NetworkChangeObserver* observer) {
  if (!observer) return;
  
  std::lock_guard<std::mutex> lock(observers_mutex_);
  observers_.erase(
    std::remove(observers_.begin(), observers_.end(), observer),
    observers_.end());
  
  Logger::instance().log(LogLevel::LOG_DEBUG,
    "NetworkChangeNotifier: Observer removed");
}

void NetworkChangeNotifier::checkForChanges() {
  ConnectionType current = getCurrentConnectionType();
  onConnectionTypeChanged(current);
}

void NetworkChangeNotifier::onConnectionTypeChanged(ConnectionType new_type) {
  ConnectionType old_type;
  
  {
    std::lock_guard<std::mutex> lock(observers_mutex_);
    old_type = last_connection_type_;
    
    // Only notify if the type actually changed
    if (new_type == old_type) {
      return;
    }
    
    last_connection_type_ = new_type;
  }
  
  Logger::instance().log(LogLevel::LOG_INFO,
    std::string("NetworkChangeNotifier: Connection type changed from ") +
    connectionTypeToString(old_type) + " to " +
    connectionTypeToString(new_type));
  
  notifyObservers(new_type);
}

void NetworkChangeNotifier::notifyObservers(ConnectionType new_type) {
  // Copy observers to avoid holding lock during callbacks
  std::vector<NetworkChangeObserver*> observers_copy;
  {
    std::lock_guard<std::mutex> lock(observers_mutex_);
    observers_copy = observers_;
  }
  
  // Notify all observers (outside the lock to prevent deadlocks)
  for (auto* observer : observers_copy) {
    observer->onNetworkChanged(new_type);
  }
}

#ifdef __ANDROID__
// Static member definitions for Android JNI
JavaVM* NetworkChangeNotifier::PlatformImpl::java_vm_ = nullptr;
jobject NetworkChangeNotifier::PlatformImpl::network_callback_obj_ = nullptr;

// JNI initialization function - should be called from Java layer
// Example usage from Java:
// NetworkChangeNotifier.nativeInit(callbackObject);
// where callbackObject implements: public int getConnectionType()
extern "C" JNIEXPORT void JNICALL
Java_com_nqe_NetworkChangeNotifier_nativeInit(JNIEnv* env, jclass clazz, jobject callback) {
  env->GetJavaVM(&NetworkChangeNotifier::PlatformImpl::java_vm_);
  if (callback) {
    // Store global reference to the callback object
    NetworkChangeNotifier::PlatformImpl::network_callback_obj_ = env->NewGlobalRef(callback);
  }
  
  Logger::instance().log(LogLevel::LOG_INFO, 
    "NetworkChangeNotifier: Android JNI initialized");
}

// JNI cleanup function
extern "C" JNIEXPORT void JNICALL
Java_com_nqe_NetworkChangeNotifier_nativeCleanup(JNIEnv* env, jclass clazz) {
  if (NetworkChangeNotifier::PlatformImpl::network_callback_obj_) {
    env->DeleteGlobalRef(NetworkChangeNotifier::PlatformImpl::network_callback_obj_);
    NetworkChangeNotifier::PlatformImpl::network_callback_obj_ = nullptr;
  }
  NetworkChangeNotifier::PlatformImpl::java_vm_ = nullptr;
  
  Logger::instance().log(LogLevel::LOG_INFO, 
    "NetworkChangeNotifier: Android JNI cleaned up");
}

// JNI callback from Java when network changes
extern "C" JNIEXPORT void JNICALL
Java_com_nqe_NetworkChangeNotifier_nativeOnNetworkChanged(JNIEnv* env, jclass clazz, jint networkType) {
  ConnectionType type = static_cast<ConnectionType>(networkType);
  NetworkChangeNotifier::instance().onConnectionTypeChanged(type);
  
  Logger::instance().log(LogLevel::LOG_DEBUG,
    std::string("NetworkChangeNotifier: Java notified network change to ") +
    connectionTypeToString(type));
}
#endif

} // namespace nqe
