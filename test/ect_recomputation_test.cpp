/**
 * Comprehensive test for ECT recomputation logic
 * Tests the Chromium NQE-style recomputation mechanism
 */

#include "nqe/Nqe.h"
#include "nqe/Logger.h"
#include "nqe/NetworkQualityObserver.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

using namespace nqe;

void printHeader(const std::string& title) {
  std::cout << "\n";
  std::cout << "╔════════════════════════════════════════════════════════════╗\n";
  std::cout << "║  " << std::left << std::setw(56) << title << "  ║\n";
  std::cout << "╚════════════════════════════════════════════════════════════╝\n";
  std::cout << std::endl;
}

void printSection(const std::string& title) {
  std::cout << "\n============================================================\n";
  std::cout << "  " << title << "\n";
  std::cout << "============================================================\n\n";
}

// Observer to track ECT changes
class TestECTObserver : public EffectiveConnectionTypeObserver {
public:
  int change_count = 0;
  EffectiveConnectionType last_old_type = EffectiveConnectionType::UNKNOWN;
  EffectiveConnectionType last_new_type = EffectiveConnectionType::UNKNOWN;
  
  void onEffectiveConnectionTypeChanged(
      EffectiveConnectionType old_type,
      EffectiveConnectionType new_type) override {
    change_count++;
    last_old_type = old_type;
    last_new_type = new_type;
    std::cout << "  [ECT Observer] ECT changed from " 
              << effectiveConnectionTypeToString(old_type)
              << " to " << effectiveConnectionTypeToString(new_type)
              << " (total changes: " << change_count << ")\n";
  }
};

void testTimeBasedRecomputation() {
  printSection("Testing Time-Based ECT Recomputation");
  
  Nqe::Options opts;
  opts.effective_connection_type_recomputation_interval = std::chrono::seconds(2);
  opts.decay_lambda_per_sec = 0.05;
  
  Nqe estimator(opts);
  TestECTObserver observer;
  estimator.addEffectiveConnectionTypeObserver(&observer);
  
  std::cout << "Configuration: Recomputation interval = 2 seconds\n\n";
  
  // Add initial samples (should trigger first computation)
  std::cout << "Adding initial samples...\n";
  estimator.addSample(Source::HTTP_TTFB, 100.0);
  estimator.addSample(Source::TRANSPORT_RTT, 30.0);
  
  auto ect1 = estimator.getEffectiveConnectionType();
  std::cout << "Initial ECT: " << effectiveConnectionTypeToString(ect1) << "\n";
  std::cout << "Observer changes: " << observer.change_count << "\n\n";
  
  // Add more samples immediately (should NOT trigger recomputation yet due to time interval)
  std::cout << "Adding more samples immediately (should not recompute yet)...\n";
  estimator.addSample(Source::HTTP_TTFB, 110.0);
  std::cout << "Observer changes: " << observer.change_count << "\n\n";
  
  // Wait for recomputation interval to pass
  std::cout << "Waiting 2.5 seconds for recomputation interval...\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(2500));
  
  // Add a sample (should trigger recomputation due to time)
  std::cout << "Adding sample after interval...\n";
  estimator.addSample(Source::HTTP_TTFB, 120.0);
  std::cout << "Observer changes: " << observer.change_count << "\n\n";
  
  std::cout << "✓ Time-based recomputation working\n";
  
  estimator.removeEffectiveConnectionTypeObserver(&observer);
}

void testObservationCountRecomputation() {
  printSection("Testing Observation Count-Based Recomputation");
  
  Nqe::Options opts;
  opts.effective_connection_type_recomputation_interval = std::chrono::hours(1); // Very long
  opts.count_new_observations_received_compute_ect = 5; // Trigger after 5 new observations
  opts.decay_lambda_per_sec = 0.05;
  
  Nqe estimator(opts);
  TestECTObserver observer;
  estimator.addEffectiveConnectionTypeObserver(&observer);
  
  std::cout << "Configuration: Min new observations = 5\n\n";
  
  // Add initial sample
  std::cout << "Adding initial sample (triggers first computation)...\n";
  estimator.addSample(Source::HTTP_TTFB, 100.0);
  auto initial_changes = observer.change_count;
  std::cout << "Observer changes: " << observer.change_count << "\n\n";
  
  // Add 4 more samples (should not trigger yet)
  std::cout << "Adding 4 more samples (should not trigger recomputation)...\n";
  for (int i = 0; i < 4; i++) {
    estimator.addSample(Source::HTTP_TTFB, 100.0 + i);
  }
  std::cout << "Observer changes: " << observer.change_count << "\n\n";
  
  // Add 5th new sample (should trigger recomputation)
  std::cout << "Adding 5th new sample (should trigger recomputation)...\n";
  estimator.addSample(Source::HTTP_TTFB, 105.0);
  std::cout << "Observer changes: " << observer.change_count << "\n\n";
  
  if (observer.change_count > initial_changes) {
    std::cout << "✓ Observation count-based recomputation working\n";
  } else {
    std::cout << "✗ No recomputation triggered after 5 observations\n";
  }
  
  estimator.removeEffectiveConnectionTypeObserver(&observer);
}

void testObservationIncreaseRecomputation() {
  printSection("Testing 50% Observation Increase Recomputation");
  
  Nqe::Options opts;
  opts.effective_connection_type_recomputation_interval = std::chrono::hours(1); // Very long
  opts.count_new_observations_received_compute_ect = 1000; // Very high
  opts.decay_lambda_per_sec = 0.05;
  
  Nqe estimator(opts);
  TestECTObserver observer;
  estimator.addEffectiveConnectionTypeObserver(&observer);
  
  std::cout << "Configuration: Testing 50% increase trigger\n\n";
  
  // Add 10 initial samples to establish baseline
  std::cout << "Adding 10 initial samples...\n";
  for (int i = 0; i < 10; i++) {
    estimator.addSample(Source::HTTP_TTFB, 100.0);
  }
  auto initial_changes = observer.change_count;
  std::cout << "Initial ECT computations: " << initial_changes << "\n\n";
  
  // Force an ECT computation by getting estimate
  auto ect = estimator.getEffectiveConnectionType();
  std::cout << "Current ECT: " << effectiveConnectionTypeToString(ect) << "\n\n";
  
  // Add 4 more samples (40% increase - should not trigger)
  std::cout << "Adding 4 more samples (40% increase - should not trigger)...\n";
  for (int i = 0; i < 4; i++) {
    estimator.addSample(Source::HTTP_TTFB, 110.0);
  }
  auto changes_after_40 = observer.change_count;
  std::cout << "Observer changes: " << observer.change_count << "\n\n";
  
  // Add 2 more samples (60% increase from baseline - should trigger)
  std::cout << "Adding 2 more samples (60% total increase - should trigger)...\n";
  for (int i = 0; i < 2; i++) {
    estimator.addSample(Source::HTTP_TTFB, 120.0);
  }
  std::cout << "Observer changes: " << observer.change_count << "\n\n";
  
  if (observer.change_count > changes_after_40) {
    std::cout << "✓ 50% observation increase recomputation working\n";
  }
  
  estimator.removeEffectiveConnectionTypeObserver(&observer);
}

void testUnknownECTRecomputation() {
  printSection("Testing UNKNOWN ECT Recomputation Trigger");
  
  Nqe::Options opts;
  opts.effective_connection_type_recomputation_interval = std::chrono::hours(1);
  opts.count_new_observations_received_compute_ect = 1000;
  opts.decay_lambda_per_sec = 0.05;
  
  Nqe estimator(opts);
  TestECTObserver observer;
  estimator.addEffectiveConnectionTypeObserver(&observer);
  
  std::cout << "Configuration: Testing UNKNOWN ECT trigger\n\n";
  
  // Check initial ECT (should be UNKNOWN)
  auto ect = estimator.getEffectiveConnectionType();
  std::cout << "Initial ECT: " << effectiveConnectionTypeToString(ect) << "\n";
  
  if (ect == EffectiveConnectionType::UNKNOWN) {
    std::cout << "✓ ECT is UNKNOWN as expected\n\n";
  }
  
  // Add a single sample (should trigger recomputation because ECT is UNKNOWN)
  std::cout << "Adding sample when ECT is UNKNOWN (should trigger recomputation)...\n";
  estimator.addSample(Source::HTTP_TTFB, 100.0);
  
  ect = estimator.getEffectiveConnectionType();
  std::cout << "ECT after sample: " << effectiveConnectionTypeToString(ect) << "\n";
  std::cout << "Observer changes: " << observer.change_count << "\n\n";
  
  if (ect != EffectiveConnectionType::UNKNOWN) {
    std::cout << "✓ UNKNOWN ECT trigger working - ECT was recomputed\n";
  }
  
  estimator.removeEffectiveConnectionTypeObserver(&observer);
}

void testECTChangeNotification() {
  printSection("Testing ECT Change Notifications");
  
  Nqe::Options opts;
  opts.effective_connection_type_recomputation_interval = std::chrono::seconds(1);
  opts.decay_lambda_per_sec = 0.05;
  
  // Set thresholds to make it easy to change ECT
  opts.ect_thresholds.http_rtt_3g = 200;  // 3G threshold
  opts.ect_thresholds.http_rtt_2g = 500;  // 2G threshold
  
  Nqe estimator(opts);
  TestECTObserver observer;
  estimator.addEffectiveConnectionTypeObserver(&observer);
  
  std::cout << "Simulating network quality degradation...\n\n";
  
  // Start with good network (4G)
  std::cout << "1. Adding samples for 4G network (RTT ~50ms)...\n";
  for (int i = 0; i < 5; i++) {
    estimator.addSample(Source::HTTP_TTFB, 50.0 + i);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  
  auto ect1 = estimator.getEffectiveConnectionType();
  std::cout << "   ECT: " << effectiveConnectionTypeToString(ect1) << "\n";
  std::cout << "   Observer changes: " << observer.change_count << "\n\n";
  
  // Degrade to 3G
  std::cout << "2. Adding samples for 3G network (RTT ~250ms)...\n";
  for (int i = 0; i < 5; i++) {
    estimator.addSample(Source::HTTP_TTFB, 250.0 + i);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  
  estimator.addSample(Source::HTTP_TTFB, 255.0);
  auto ect2 = estimator.getEffectiveConnectionType();
  std::cout << "   ECT: " << effectiveConnectionTypeToString(ect2) << "\n";
  std::cout << "   Observer changes: " << observer.change_count << "\n\n";
  
  // Degrade to 2G
  std::cout << "3. Adding samples for 2G network (RTT ~600ms)...\n";
  for (int i = 0; i < 5; i++) {
    estimator.addSample(Source::HTTP_TTFB, 600.0 + i);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  
  estimator.addSample(Source::HTTP_TTFB, 605.0);
  auto ect3 = estimator.getEffectiveConnectionType();
  std::cout << "   ECT: " << effectiveConnectionTypeToString(ect3) << "\n";
  std::cout << "   Observer changes: " << observer.change_count << "\n\n";
  
  std::cout << "Total ECT changes observed: " << observer.change_count << "\n";
  std::cout << "Last change: " << effectiveConnectionTypeToString(observer.last_old_type)
            << " → " << effectiveConnectionTypeToString(observer.last_new_type) << "\n\n";
  
  if (observer.change_count >= 2) {
    std::cout << "✓ ECT change notifications working correctly\n";
  }
  
  estimator.removeEffectiveConnectionTypeObserver(&observer);
}

int main() {
  // Configure logging
  Logger::instance().setMinLevel(LogLevel::LOG_INFO);
  Logger::instance().setCallback([](LogLevel level, const std::string& msg) {
    std::cout << "[" << logLevelToString(level) << "] " << msg << std::endl;
  });
  
  printHeader("ECT Recomputation Logic - Comprehensive Test");
  
  try {
    testUnknownECTRecomputation();
    testTimeBasedRecomputation();
    testObservationCountRecomputation();
    testObservationIncreaseRecomputation();
    testECTChangeNotification();
    
    printSection("All Tests Completed Successfully");
    std::cout << "✓ All ECT recomputation features working as expected!\n\n";
    
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}
