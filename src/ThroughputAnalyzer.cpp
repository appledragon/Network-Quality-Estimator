#include "nqe/ThroughputAnalyzer.h"
#include "nqe/Logger.h"
#include "aggregators/Aggregator.h"
#include <chrono>
#include <algorithm>
#include <cmath>

namespace nqe {

ThroughputAnalyzer::ThroughputAnalyzer() 
    : ThroughputAnalyzer(Options{}) {
}

ThroughputAnalyzer::ThroughputAnalyzer(const Options& opts) 
    : opts_(opts),
      aggregator_(std::make_unique<Aggregator>(opts.decay_lambda_per_sec)) {
  Logger::instance().info("ThroughputAnalyzer initialized with decay_lambda=", 
                          opts_.decay_lambda_per_sec,
                          ", min_transfer_size=", opts_.min_transfer_size_bytes,
                          ", min_requests_in_flight=", opts_.throughput_min_requests_in_flight);
}

ThroughputAnalyzer::~ThroughputAnalyzer() = default;

bool ThroughputAnalyzer::shouldDiscardRequest(const Request& request) const {
  // Only GET requests are used for throughput calculation
  // Matching Chromium's behavior: return request.method() != "GET"
  return request.method != "GET";
}

bool ThroughputAnalyzer::degradesAccuracy(const Request& request) const {
  // Requests that degrade accuracy:
  // 1. Cached responses (not actual network transfer)
  if (request.is_cached) {
    return true;
  }
  
  // 2. Hanging/stalled requests
  if (request.is_hanging) {
    return true;
  }
  
  // Note: Size check is done during observation, not at start
  // Requests can accumulate bytes over time
  
  return false;
}

void ThroughputAnalyzer::notifyStartTransaction(const Request& request) {
  std::lock_guard<std::mutex> lk(mu_);
  
  // Check if we should discard this request entirely
  if (shouldDiscardRequest(request)) {
    Logger::instance().debug("Discarding request (method=", request.method, ")");
    return;
  }
  
  // Check if this request degrades accuracy
  if (degradesAccuracy(request)) {
    accuracy_degrading_requests_.insert(request);
    endThroughputObservationWindow();
    Logger::instance().debug("Request degrades accuracy, ending observation window");
    return;
  }
  
  // Add to active requests with monotonic sequence ID for proper age ordering
  Request tracked = request;
  tracked.sequence_id = next_sequence_id_++;
  requests_.insert(tracked);
  
  // Limit the size of tracked requests by removing oldest (by ID ordering)
  // Note: std::set orders by Request::id (pointer value), not chronologically.
  // This is acceptable as we just need to limit total memory usage.
  // The observation window mechanism will ensure accurate throughput calculation
  // based on requests active during the window, regardless of removal order.
  if (requests_.size() > kMaxRequestsSize) {
    auto it = requests_.begin();
    std::advance(it, requests_.size() - kMaxRequestsSize);
    requests_.erase(requests_.begin(), it);
  }
  
  // Try to start observation window
  maybeStartThroughputObservationWindow();
  
  Logger::instance().debug("Started tracking request (active=", requests_.size(),
                           ", degrading=", accuracy_degrading_requests_.size(), ")");
}

void ThroughputAnalyzer::notifyBytesRead(const void* request_id, size_t bytes_read) {
  std::lock_guard<std::mutex> lk(mu_);
  
  // Find the request in active requests
  auto it = std::find_if(requests_.begin(), requests_.end(),
                        [request_id](const Request& r) { return r.id == request_id; });
  
  if (it != requests_.end()) {
    // Update bytes received
    Request updated = *it;
    updated.bytes_received += bytes_read;
    
    // Check if the update causes the request to degrade accuracy
    if (degradesAccuracy(updated)) {
      requests_.erase(it);
      accuracy_degrading_requests_.insert(updated);
      endThroughputObservationWindow();
      Logger::instance().debug("Request now degrades accuracy after byte update");
      return;
    }
    
    // Update the request
    requests_.erase(it);
    requests_.insert(updated);
    return;
  }
  
  // Check if it's in degrading requests
  auto it_deg = std::find_if(accuracy_degrading_requests_.begin(), 
                             accuracy_degrading_requests_.end(),
                             [request_id](const Request& r) { return r.id == request_id; });
  
  if (it_deg != accuracy_degrading_requests_.end()) {
    Request updated = *it_deg;
    updated.bytes_received += bytes_read;
    accuracy_degrading_requests_.erase(it_deg);
    accuracy_degrading_requests_.insert(updated);
  }
}

void ThroughputAnalyzer::notifyRequestCompleted(const void* request_id) {
  std::lock_guard<std::mutex> lk(mu_);
  
  // Try to get throughput observation if we're in a window
  if (in_observation_window_) {
    maybeGetThroughputObservation();
  }
  
  // Remove from active requests
  auto it = std::find_if(requests_.begin(), requests_.end(),
                        [request_id](const Request& r) { return r.id == request_id; });
  
  if (it != requests_.end()) {
    requests_.erase(it);
    Logger::instance().debug("Request completed, removed from active set (active=", 
                            requests_.size(), ")");
    
    // Window may need to end if we don't have enough requests anymore
    if (in_observation_window_ && 
        requests_.size() < opts_.throughput_min_requests_in_flight) {
      endThroughputObservationWindow();
    }
    return;
  }
  
  // Remove from degrading requests
  auto it_deg = std::find_if(accuracy_degrading_requests_.begin(), 
                             accuracy_degrading_requests_.end(),
                             [request_id](const Request& r) { return r.id == request_id; });
  
  if (it_deg != accuracy_degrading_requests_.end()) {
    accuracy_degrading_requests_.erase(it_deg);
    Logger::instance().debug("Request completed, removed from degrading set (degrading=", 
                            accuracy_degrading_requests_.size(), ")");
    
    // Try to start window if no more degrading requests
    if (accuracy_degrading_requests_.empty()) {
      maybeStartThroughputObservationWindow();
    }
  }
}

void ThroughputAnalyzer::onConnectionTypeChanged() {
  std::lock_guard<std::mutex> lk(mu_);
  
  Logger::instance().info("Connection type changed, resetting throughput observation");
  
  // Move all active requests to degrading set
  accuracy_degrading_requests_.insert(requests_.begin(), requests_.end());
  requests_.clear();
  
  // End current observation window
  endThroughputObservationWindow();
}

void ThroughputAnalyzer::maybeStartThroughputObservationWindow() {
  // Already in window
  if (in_observation_window_) {
    return;
  }
  
  // Need no degrading requests
  if (!accuracy_degrading_requests_.empty()) {
    return;
  }
  
  // Need minimum requests in flight
  if (requests_.size() < opts_.throughput_min_requests_in_flight) {
    return;
  }
  
  // Start the window
  in_observation_window_ = true;
  window_start_time_ = Clock::now();
  bits_received_at_window_start_ = getBitsReceived();
  
  Logger::instance().debug("Started throughput observation window (", 
                          requests_.size(), " requests in flight)");
}

void ThroughputAnalyzer::endThroughputObservationWindow() {
  if (!in_observation_window_) {
    return;
  }
  
  in_observation_window_ = false;
  Logger::instance().debug("Ended throughput observation window");
}

void ThroughputAnalyzer::maybeGetThroughputObservation() {
  if (!in_observation_window_) {
    return;
  }
  
  auto now = Clock::now();
  size_t bits_received = getBitsReceived();
  
  // Check for underflow - bits should only increase
  if (bits_received < bits_received_at_window_start_) {
    Logger::instance().debug("Bits received decreased, ending window");
    endThroughputObservationWindow();
    return;
  }
  
  // Calculate bits received during window
  size_t bits_in_window = bits_received - bits_received_at_window_start_;
  
  // Ignore if too small
  if (bits_in_window < opts_.min_transfer_size_bytes * 8) {
    Logger::instance().debug("Window too small for observation");
    return;
  }
  
  // Calculate duration in milliseconds
  auto duration = std::chrono::duration<double, std::milli>(now - window_start_time_);
  double duration_ms = duration.count();
  
  if (duration_ms <= 0) {
    Logger::instance().debug("Window duration non-positive");
    return;
  }
  
  // Calculate throughput in kbps: bits / duration_ms
  // Chromium uses: (bits_received * 1.0f) / duration.InMillisecondsF()
  double downstream_kbps_double = (bits_in_window * 1.0) / duration_ms;
  
  // Round up (ceil) like Chromium
  int64_t downstream_kbps = static_cast<int64_t>(std::ceil(downstream_kbps_double));
  
  // Add the observation
  aggregator_->add(static_cast<double>(downstream_kbps), now);
  
  Logger::instance().debug("Throughput observation: ", downstream_kbps, " kbps (",
                          bits_in_window / 8, " bytes in ", duration_ms, " ms)");
  
  // End the window and potentially start a new one
  endThroughputObservationWindow();
  maybeStartThroughputObservationWindow();
}

size_t ThroughputAnalyzer::getBitsReceived() const {
  size_t total_bits = 0;
  constexpr size_t BITS_PER_BYTE = 8;
  for (const auto& req : requests_) {
    total_bits += req.bytes_received * BITS_PER_BYTE;
  }
  return total_bits;
}

bool ThroughputAnalyzer::isHangingWindow(
    size_t bits_received,
    std::chrono::milliseconds window_duration,
    double http_rtt_ms) const {
  
  if (!opts_.enable_hanging_window_detection) {
    return false;  // Feature disabled
  }
  
  if (http_rtt_ms <= 0 || window_duration.count() <= 0) {
    return false;  // Invalid inputs
  }
  
  // Calculate expected bits per HTTP RTT
  // If window received X bits in duration D, then in time T (HTTP RTT) it would receive:
  // bits_per_http_rtt = X * (T / D)
  double bits_per_http_rtt = static_cast<double>(bits_received) * 
                              (http_rtt_ms / window_duration.count());
  
  // Calculate minimum expected bits based on congestion window
  // Chrome uses: cwnd_size * multiplier
  // Default: 10 KB * 0.5 = 5 KB = 40 Kbits
  constexpr size_t BITS_PER_BYTE = 8;
  size_t cwnd_bits = opts_.hanging_window_cwnd_size_kb * 1024 * BITS_PER_BYTE;
  double min_expected_bits = cwnd_bits * opts_.hanging_window_cwnd_multiplier;
  
  // Window is hanging if bits received per HTTP RTT is less than threshold
  bool is_hanging = bits_per_http_rtt < min_expected_bits;
  
  if (is_hanging) {
    Logger::instance().debug("Hanging window detected: ",
                             "bits_per_http_rtt=", static_cast<int64_t>(bits_per_http_rtt),
                             ", min_expected=", static_cast<int64_t>(min_expected_bits),
                             ", http_rtt=", http_rtt_ms, "ms");
  }
  
  return is_hanging;
}

void ThroughputAnalyzer::addTransfer(size_t bytes_received, TimePoint start_time, TimePoint end_time) {
  constexpr size_t BITS_PER_BYTE = 8;
  constexpr size_t BITS_PER_KBIT = 1000;
  
  // Ignore transfers that are too small (unreliable for throughput estimation)
  if (bytes_received < opts_.min_transfer_size_bytes) {
    Logger::instance().debug("Ignoring small transfer: ", bytes_received, " bytes");
    return;
  }
  
  // Calculate duration in seconds
  auto duration = std::chrono::duration<double>(end_time - start_time).count();
  if (duration <= 0) {
    Logger::instance().debug("Ignoring transfer with non-positive duration");
    return;
  }
  
  // Calculate throughput in kbps: (bytes * 8) / (duration * 1000)
  double throughput_kbps = (bytes_received * BITS_PER_BYTE) / (duration * BITS_PER_KBIT);
  
  std::lock_guard<std::mutex> lk(mu_);
  aggregator_->add(throughput_kbps, end_time);
  
  Logger::instance().debug("Added throughput sample: ", throughput_kbps, " kbps (",
                           bytes_received, " bytes in ", duration, " sec)");
}

std::optional<double> ThroughputAnalyzer::getEstimate(TimePoint now) const {
  std::lock_guard<std::mutex> lk(mu_);
  
  auto estimate = aggregator_->estimate(now);
  if (!estimate) {
    return std::nullopt;
  }
  
  // Check if the estimate is fresh
  auto age = now - aggregator_->latestTs();
  if (age > opts_.freshness_threshold) {
    return std::nullopt;
  }
  
  return estimate;
}

ThroughputAnalyzer::Statistics ThroughputAnalyzer::getStatistics(TimePoint now) const {
  std::lock_guard<std::mutex> lk(mu_);
  
  Statistics stats;
  stats.sample_count = aggregator_->sampleCount();
  stats.min_kbps = aggregator_->minValue();
  stats.max_kbps = aggregator_->maxValue();
  stats.percentile_50th = aggregator_->percentile(now, 0.50);
  stats.percentile_95th = aggregator_->percentile(now, 0.95);
  stats.percentile_99th = aggregator_->percentile(now, 0.99);
  stats.last_sample_time = aggregator_->latestTs();
  stats.active_requests = requests_.size();
  stats.degrading_requests = accuracy_degrading_requests_.size();
  
  return stats;
}

size_t ThroughputAnalyzer::getSampleCount() const {
  std::lock_guard<std::mutex> lk(mu_);
  return aggregator_->sampleCount();
}

} // namespace nqe
