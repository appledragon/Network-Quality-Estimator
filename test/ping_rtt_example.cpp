// Example demonstrating the QuicH2PingSource helper for PING RTT tracking
#include "nqe/Nqe.h"
#include "nqe/QuicH2PingSource.h"
#include "nqe/Logger.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <random>

using namespace std::chrono_literals;

int main() {
  // Setup logging
  nqe::Logger::instance().setMinLevel(nqe::LogLevel::LOG_INFO);
  nqe::Logger::instance().setCallback([](nqe::LogLevel level, const std::string& msg) {
    std::cout << "[" << nqe::logLevelToString(level) << "] " << msg << std::endl;
  });

  // Create NQE instance
  nqe::Nqe estimator;
  nqe::QuicH2PingSource ping_source(estimator);

  // Random number generator for simulating variable latency
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> latency_dist(10, 50);

  // Setup a mock PING implementation
  ping_source.setPingImpl([](const std::string& authority) {
    std::cout << "Sending PING to: " << authority << std::endl;
    // In a real implementation, this would send an actual H2/QUIC PING frame
  });

  // Simulate PING/PONG exchanges
  std::cout << "\nSimulating PING/PONG exchanges..." << std::endl;
  
  std::vector<std::string> authorities = {
    "example.com:443",
    "google.com:443",
    "cloudflare.com:443"
  };

  for (int round = 0; round < 3; ++round) {
    std::cout << "\n--- Round " << (round + 1) << " ---" << std::endl;
    
    for (const auto& authority : authorities) {
      // Send PING
      ping_source.ping(authority);
      
      // Simulate network delay
      int delay_ms = latency_dist(gen);
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
      
      // Simulate receiving PONG
      ping_source.onPong(authority);
      std::cout << "Received PONG from: " << authority 
                << " (simulated RTT: ~" << delay_ms << "ms)" << std::endl;
    }
    
    // Get current estimate
    auto estimate = estimator.getEstimate();
    std::cout << "\nCurrent RTT estimate: " << estimate.rtt_ms << "ms";
    if (estimate.ping_rtt_ms) {
      std::cout << " (PING RTT: " << *estimate.ping_rtt_ms << "ms)";
    }
    std::cout << std::endl;
    
    std::this_thread::sleep_for(500ms);
  }

  // Print final statistics
  std::cout << "\n=== Final Statistics ===" << std::endl;
  auto stats = estimator.getStatistics();
  std::cout << "Total samples: " << stats.total_samples << std::endl;
  
  if (stats.ping.sample_count > 0) {
    std::cout << "PING Statistics:" << std::endl;
    std::cout << "  Sample count: " << stats.ping.sample_count << std::endl;
    std::cout << "  Min: " << *stats.ping.min_ms << "ms" << std::endl;
    std::cout << "  Median (p50): " << *stats.ping.percentile_50th << "ms" << std::endl;
    std::cout << "  p95: " << *stats.ping.percentile_95th << "ms" << std::endl;
    std::cout << "  Max: " << *stats.ping.max_ms << "ms" << std::endl;
  }

  return 0;
}
