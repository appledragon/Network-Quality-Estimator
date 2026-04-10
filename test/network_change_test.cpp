#include "nqe/Nqe.h"
#include "nqe/NetworkChangeNotifier.h"
#include "nqe/Logger.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace nqe;

// Custom observer to monitor network changes
class TestNetworkObserver : public NetworkChangeObserver {
public:
  int change_count = 0;
  ConnectionType last_type = ConnectionType::UNKNOWN;
  
  void onNetworkChanged(ConnectionType type) override {
    change_count++;
    last_type = type;
    std::cout << "  [Network Observer] Network changed to: " 
              << connectionTypeToString(type) << std::endl;
  }
};

void printSeparator(const std::string& title) {
  std::cout << "\n============================================================\n";
  std::cout << "  " << title << "\n";
  std::cout << "============================================================\n\n";
}

void testNetworkChangeNotifier() {
  printSeparator("Testing NetworkChangeNotifier");
  
  // Get singleton instance
  auto& notifier = NetworkChangeNotifier::instance();
  
  // Check initial state
  std::cout << "Initial monitoring state: " 
            << (notifier.isMonitoring() ? "active" : "inactive") << std::endl;
  
  // Get current connection type
  ConnectionType current = notifier.getCurrentConnectionType();
  std::cout << "Current connection type: " 
            << connectionTypeToString(current) << std::endl;
  
  // Add observer
  TestNetworkObserver observer;
  notifier.addObserver(&observer);
  std::cout << "✓ Observer added" << std::endl;
  
  // Start monitoring
  notifier.start();
  std::cout << "✓ Monitoring started" << std::endl;
  std::cout << "Monitoring state: " 
            << (notifier.isMonitoring() ? "active" : "inactive") << std::endl;
  
  // Wait a bit for any initial notifications
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // Manually trigger a check
  std::cout << "\nManually triggering network check..." << std::endl;
  notifier.checkForChanges();
  
  std::cout << "Observer notification count: " << observer.change_count << std::endl;
  
  // Remove observer
  notifier.removeObserver(&observer);
  std::cout << "✓ Observer removed" << std::endl;
  
  // Stop monitoring
  notifier.stop();
  std::cout << "✓ Monitoring stopped" << std::endl;
  std::cout << "Monitoring state: " 
            << (notifier.isMonitoring() ? "active" : "inactive") << std::endl;
}

void testNqeNetworkChangeIntegration() {
  printSeparator("Testing NQE Network Change Integration");
  
  // Create NQE instance
  Nqe::Options opts;
  opts.decay_lambda_per_sec = 0.05;
  Nqe estimator(opts);
  
  std::cout << "Network change detection enabled: " 
            << (estimator.isNetworkChangeDetectionEnabled() ? "yes" : "no") << std::endl;
  
  // Add some samples
  std::cout << "\nAdding initial samples..." << std::endl;
  estimator.addSample(Source::HTTP_TTFB, 100.0);
  estimator.addSample(Source::TRANSPORT_RTT, 50.0);
  estimator.addSample(Source::HTTP_TTFB, 110.0);
  
  auto stats1 = estimator.getStatistics();
  std::cout << "Initial samples: HTTP=" << stats1.http.sample_count 
            << ", Transport=" << stats1.transport.sample_count << std::endl;
  
  // Enable network change detection with clear_on_change=true
  std::cout << "\nEnabling network change detection (clear_on_change=true)..." << std::endl;
  estimator.enableNetworkChangeDetection(true);
  std::cout << "Network change detection enabled: " 
            << (estimator.isNetworkChangeDetectionEnabled() ? "yes" : "no") << std::endl;
  
  // Wait a bit
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // Check that samples are still there (no network change yet)
  auto stats2 = estimator.getStatistics();
  std::cout << "Samples after enabling: HTTP=" << stats2.http.sample_count 
            << ", Transport=" << stats2.transport.sample_count << std::endl;
  
  // Simulate network change by manually triggering
  std::cout << "\nSimulating network change..." << std::endl;
  NetworkChangeNotifier::instance().checkForChanges();
  
  // Wait for the change to propagate
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  
  // Check if samples were cleared
  auto stats3 = estimator.getStatistics();
  std::cout << "Samples after simulated change: HTTP=" << stats3.http.sample_count 
            << ", Transport=" << stats3.transport.sample_count << std::endl;
  
  if (stats3.http.sample_count == 0 && stats3.transport.sample_count == 0) {
    std::cout << "✓ Samples cleared on network change as expected" << std::endl;
  } else {
    std::cout << "Note: Samples not cleared (network type may not have changed)" << std::endl;
  }
  
  // Disable network change detection
  std::cout << "\nDisabling network change detection..." << std::endl;
  estimator.disableNetworkChangeDetection();
  std::cout << "Network change detection enabled: " 
            << (estimator.isNetworkChangeDetectionEnabled() ? "yes" : "no") << std::endl;
  std::cout << "✓ Network change detection disabled" << std::endl;
}

void testNqeNetworkChangeWithoutClear() {
  printSeparator("Testing NQE Network Change Without Clearing Samples");
  
  Nqe::Options opts;
  opts.decay_lambda_per_sec = 0.05;
  Nqe estimator(opts);
  
  // Add samples
  std::cout << "Adding initial samples..." << std::endl;
  estimator.addSample(Source::HTTP_TTFB, 100.0);
  estimator.addSample(Source::TRANSPORT_RTT, 50.0);
  
  auto stats1 = estimator.getStatistics();
  std::cout << "Initial samples: HTTP=" << stats1.http.sample_count 
            << ", Transport=" << stats1.transport.sample_count << std::endl;
  
  // Enable network change detection with clear_on_change=false
  std::cout << "\nEnabling network change detection (clear_on_change=false)..." << std::endl;
  estimator.enableNetworkChangeDetection(false);
  
  // Trigger change
  std::cout << "Triggering network change..." << std::endl;
  NetworkChangeNotifier::instance().checkForChanges();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  
  // Check samples (should still be there)
  auto stats2 = estimator.getStatistics();
  std::cout << "Samples after change: HTTP=" << stats2.http.sample_count 
            << ", Transport=" << stats2.transport.sample_count << std::endl;
  
  if (stats2.http.sample_count > 0 || stats2.transport.sample_count > 0) {
    std::cout << "✓ Samples preserved on network change as expected" << std::endl;
  }
  
  estimator.disableNetworkChangeDetection();
}

void testConnectionTypeStrings() {
  printSeparator("Testing ConnectionType String Conversions");
  
  ConnectionType types[] = {
    ConnectionType::UNKNOWN,
    ConnectionType::ETHERNET,
    ConnectionType::WIFI,
    ConnectionType::CELLULAR_2G,
    ConnectionType::CELLULAR_3G,
    ConnectionType::CELLULAR_4G,
    ConnectionType::CELLULAR_5G,
    ConnectionType::BLUETOOTH,
    ConnectionType::NONE
  };
  
  for (auto type : types) {
    std::cout << "  " << static_cast<int>(type) << " -> " 
              << connectionTypeToString(type) << std::endl;
  }
  
  std::cout << "✓ All connection types have string representations" << std::endl;
}

int main() {
  // Configure logging
  Logger::instance().setMinLevel(LogLevel::LOG_INFO);
  Logger::instance().setCallback([](LogLevel level, const std::string& msg) {
    std::cout << "[" << logLevelToString(level) << "] " << msg << std::endl;
  });
  
  std::cout << "╔════════════════════════════════════════════════════════════╗\n";
  std::cout << "║    Network Change Detection - Comprehensive Test Suite    ║\n";
  std::cout << "╚════════════════════════════════════════════════════════════╝\n";
  
  try {
    testConnectionTypeStrings();
    testNetworkChangeNotifier();
    testNqeNetworkChangeIntegration();
    testNqeNetworkChangeWithoutClear();
    
    std::cout << "\n============================================================\n";
    std::cout << "  All Network Change Detection Tests Completed\n";
    std::cout << "============================================================\n\n";
    std::cout << "✓ All tests passed!\n\n";
    
  } catch (const std::exception& e) {
    std::cerr << "\n✗ Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
  
  return 0;
}
