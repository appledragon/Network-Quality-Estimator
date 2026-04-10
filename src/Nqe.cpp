#include "nqe/Nqe.h"
#include "nqe/Logger.h"
#include "nqe/EffectiveConnectionType.h"
#include "nqe/ThroughputAnalyzer.h"
#include "nqe/NetworkQualityObserver.h"
#include "nqe/NetworkID.h"
#include "nqe/CachedNetworkQuality.h"
#include "nqe/NetworkQualityStore.h"
#ifndef NQE_DISABLE_NETWORK_CHANGE_NOTIFIER
#include "nqe/NetworkChangeNotifier.h"
#endif
#include "aggregators/Aggregator.h"
#include "aggregators/Combiner.h"

#include <algorithm>
#include <atomic>
#include <set>
#include <thread>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <mstcpip.h>
  #ifndef SIO_TCP_INFO
    #define SIO_TCP_INFO _WSAIOW(IOC_VENDOR,39)
  #endif
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/tcp.h>
  #include <netinet/in.h>
  #include <unistd.h>
#endif

namespace nqe {

namespace {
// Utility function to calculate throughput in kbps from bytes and duration.
// Formula: (bytes * 8) / (duration_sec * 1000) = bits / ms = kbps
// (since 1 kbps = 1000 bits/sec = 1 bit/ms)
double calculateThroughputKbps(size_t bytes, TimePoint start_time, TimePoint end_time) {
  auto duration = std::chrono::duration<double>(end_time - start_time).count();
  if (duration <= 0) {
    return 0.0;
  }
  return (bytes * 8.0) / (duration * 1000.0);
}
} // anonymous namespace

class Nqe::TransportSamplerImpl {
public:
  TransportSamplerImpl(Nqe* owner, std::chrono::milliseconds period)
    : owner_(owner), period_(period) {}

  void start() {
    run_.store(true);
    th_ = std::thread([this]{ loop(); });
  }
  void stop() {
    run_.store(false);
    if (th_.joinable()) th_.join();
  }

  void addSocket(SocketHandle s) {
    std::lock_guard<std::mutex> lk(mu_);
    socks_.insert(s);
  }
  void removeSocket(SocketHandle s) {
    std::lock_guard<std::mutex> lk(mu_);
    socks_.erase(s);
  }
  
  size_t socketCount() const {
    std::lock_guard<std::mutex> lk(mu_);
    return socks_.size();
  }
  
  bool isRunning() const {
    return run_.load();
  }

private:
  void loop() {
    while (run_.load()) {
      sampleOnce();
      std::this_thread::sleep_for(period_);
    }
  }

  void sampleOnce() {
    std::vector<SocketHandle> copy;
    {
      std::lock_guard<std::mutex> lk(mu_);
      copy.assign(socks_.begin(), socks_.end());
    }
    auto now = Clock::now();
#ifdef _WIN32
    for (auto s : copy) {
      TCP_INFO_v0 info{};
      DWORD bytes = 0;
      int rc = WSAIoctl((SOCKET)s, SIO_TCP_INFO, nullptr, 0, &info, sizeof(info), &bytes, nullptr, nullptr);
      if (rc == 0 && bytes >= sizeof(info)) {
        double rtt_ms = info.RttUs / 1000.0;
        if (rtt_ms > 0) owner_->addSample(Source::TRANSPORT_RTT, rtt_ms, now);
      }
    }
#else
    for (auto s : copy) {
      double rtt_ms = -1.0;
    #if defined(__linux__)
      struct tcp_info ti{};
      socklen_t len = sizeof(ti);
      if (getsockopt(s, SOL_TCP, TCP_INFO, &ti, &len) == 0) {
        rtt_ms = ti.tcpi_rtt / 1000.0; // 微秒 -> 毫秒
      }
    #elif defined(__APPLE__)
      // On macOS, tcp_info and TCP_INFO may not be available in userspace
      // Additionally, field names differ from Linux
      #ifdef TCP_INFO
      struct tcp_info ti{};
      socklen_t len = sizeof(ti);
      if (getsockopt(s, IPPROTO_TCP, TCP_INFO, &ti, &len) == 0) {
        // macOS tcp_info struct has different field names than Linux
        // tcpi_rtt should be available on macOS (in microseconds like Linux)
        rtt_ms = ti.tcpi_rtt / 1000.0; // microseconds -> milliseconds
      }
      #else
      // TCP_INFO not available on this macOS version, skip RTT collection
      // This is a safe fallback that allows the build to succeed
      #endif
    #else
      // 其他 POSIX 平台按需扩展
    #endif
      if (rtt_ms > 0) owner_->addSample(Source::TRANSPORT_RTT, rtt_ms, now);
    }
#endif
  }

  Nqe* owner_;
  std::chrono::milliseconds period_;
  std::atomic<bool> run_{false};
  std::thread th_;
  mutable std::mutex mu_;
  std::set<SocketHandle> socks_;
};

Nqe::Nqe() : Nqe(Options{}) {}

Nqe::Nqe(const Options& opts) : opts_(opts) {
  // Create aggregators with signal strength support if enabled
  if (opts_.enable_signal_strength_weighting) {
    http_ = std::make_unique<Aggregator>(opts_.decay_lambda_per_sec, opts_.weight_multiplier_per_signal_level);
    transport_ = std::make_unique<Aggregator>(opts_.decay_lambda_per_sec, opts_.weight_multiplier_per_signal_level);
    ping_ = std::make_unique<Aggregator>(opts_.decay_lambda_per_sec, opts_.weight_multiplier_per_signal_level);
    end_to_end_ = std::make_unique<Aggregator>(opts_.decay_lambda_per_sec, opts_.weight_multiplier_per_signal_level);
  } else {
    http_ = std::make_unique<Aggregator>(opts_.decay_lambda_per_sec);
    transport_ = std::make_unique<Aggregator>(opts_.decay_lambda_per_sec);
    ping_ = std::make_unique<Aggregator>(opts_.decay_lambda_per_sec);
    end_to_end_ = std::make_unique<Aggregator>(opts_.decay_lambda_per_sec);
  }
  
  ThroughputAnalyzer::Options tp_opts;
  tp_opts.decay_lambda_per_sec = opts_.decay_lambda_per_sec;
  tp_opts.freshness_threshold = opts_.freshness_threshold;
  tp_opts.min_transfer_size_bytes = opts_.min_throughput_transfer_bytes;
  tp_opts.throughput_min_requests_in_flight = opts_.throughput_min_requests_in_flight;
  throughput_ = std::make_unique<ThroughputAnalyzer>(tp_opts);
  
  // Initialize cache store if caching is enabled
  if (opts_.enable_caching) {
    NetworkQualityStore::Options cache_opts;
    cache_opts.max_cache_size = opts_.max_cache_size;
    cache_opts.max_cache_age = opts_.max_cache_age;
    cache_opts.storage_file_path = opts_.cache_file_path;
    cache_store_ = std::make_unique<NetworkQualityStore>(cache_opts);
    
    Logger::instance().info("NQE cache enabled: max_size=", cache_opts.max_cache_size,
                            ", max_age=", cache_opts.max_cache_age.count(), "h");
  }
  
  sampler_ = std::make_unique<TransportSamplerImpl>(this, opts_.transport_sample_period);
  
  Logger::instance().info("NQE initialized with decay_lambda=", opts_.decay_lambda_per_sec,
                          ", sample_period=", opts_.transport_sample_period.count(), "ms",
                          ", bias=", opts_.combine_bias_to_lower);
}

#ifndef NQE_DISABLE_NETWORK_CHANGE_NOTIFIER
// NetworkChangeObserver implementation for NQE
class Nqe::NetworkChangeObserverImpl : public NetworkChangeObserver {
public:
  explicit NetworkChangeObserverImpl(Nqe* parent) : parent_(parent) {}
  
  void onNetworkChanged(ConnectionType type) override {
    Logger::instance().info("Nqe: Network changed to ",
                            connectionTypeToString(type));
    parent_->onNetworkChange();
  }
  
private:
  Nqe* parent_;
};
#endif

Nqe::~Nqe() {
  disableNetworkChangeDetection();
  stopTransportSampler();
  // sampler_ is automatically cleaned up by unique_ptr
  // network_observer_ is automatically cleaned up by unique_ptr
  Logger::instance().info("NQE destroyed");
}

void Nqe::addSample(Source src, double rtt_ms, TimePoint ts) {
  const char* src_name = "Unknown";
  
  // Map source to aggregator and name
  switch (src) {
    case Source::HTTP_TTFB:
      src_name = "HTTP";
      break;
    case Source::TRANSPORT_RTT:
    case Source::TCP:
    case Source::QUIC:
      src_name = (src == Source::TCP) ? "TCP" : 
                 (src == Source::QUIC) ? "QUIC" : "Transport";
      break;
    case Source::PING_RTT:
      src_name = "Ping";
      break;
    case Source::H2_PINGS:
    case Source::H3_PINGS:
      src_name = (src == Source::H2_PINGS) ? "H2_PING" : "H3_PING";
      break;
    case Source::HTTP_CACHED_ESTIMATE:
      src_name = "HTTP_Cached";
      break;
    case Source::TRANSPORT_CACHED_ESTIMATE:
      src_name = "Transport_Cached";
      break;
    case Source::DEFAULT_HTTP_FROM_PLATFORM:
      src_name = "HTTP_Platform";
      break;
    case Source::DEFAULT_TRANSPORT_FROM_PLATFORM:
      src_name = "Transport_Platform";
      break;
  }
  
  Logger::instance().debug("Adding sample: ", src_name, " = ", rtt_ms, "ms");
  
  {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    
    // Add to appropriate aggregator
    switch (src) {
      case Source::HTTP_TTFB:
      case Source::HTTP_CACHED_ESTIMATE:
      case Source::DEFAULT_HTTP_FROM_PLATFORM:
        http_->add(rtt_ms, ts);
        break;
        
      case Source::TRANSPORT_RTT:
      case Source::TCP:
      case Source::QUIC:
      case Source::TRANSPORT_CACHED_ESTIMATE:
      case Source::DEFAULT_TRANSPORT_FROM_PLATFORM:
        transport_->add(rtt_ms, ts);
        break;
        
      case Source::PING_RTT:
        ping_->add(rtt_ms, ts);
        break;
        
      case Source::H2_PINGS:
      case Source::H3_PINGS:
        // End-to-end RTT from PING frames
        end_to_end_->add(rtt_ms, ts);
        break;
    }
    
    rtt_observations_since_last_ect_computation_++;
  }
  
  // Notify observers outside the lock
  notifyRTTObservers(rtt_ms, src_name);
  
  // Maybe recompute ECT based on new observation
  maybeComputeEffectiveConnectionType(ts);
}

Estimate Nqe::combineLocked(TimePoint now) {
  auto http = http_->estimate(now);
  auto tr   = transport_->estimate(now);
  auto ping = ping_->estimate(now);
  auto end_to_end = end_to_end_->estimate(now);

  auto fresh = [&](const Aggregator& ag, std::optional<double> v)->std::optional<double>{
    if (!v) return std::nullopt;
    auto age = now - ag.latestTs();
    if (age > opts_.freshness_threshold) return std::nullopt;
    return v;
  };
  http = fresh(*http_, http);
  tr   = fresh(*transport_, tr);
  ping = fresh(*ping_, ping);
  end_to_end = fresh(*end_to_end_, end_to_end);

  // Get sample counts for HTTP RTT bounding logic
  size_t transport_count = transport_->sampleCount();
  size_t end_to_end_count = end_to_end_->sampleCount();
  
  // Apply HTTP RTT bounding using transport and end-to-end RTT (Chrome NQE-style)
  updateHttpRttUsingAllRttValues(&http, tr, end_to_end, transport_count, end_to_end_count);
  
  // Compute preliminary ECT for HTTP RTT adjustment
  EffectiveConnectionType preliminary_ect = computeEffectiveConnectionType(
      http, tr, std::nullopt, opts_.ect_thresholds);
  
  // Adjust HTTP RTT based on RTT counts (Chrome NQE-style)
  adjustHttpRttBasedOnRTTCounts(&http, transport_count, preliminary_ect);

  Estimate e;
  e.http_ttfb_ms = http;
  e.transport_rtt_ms = tr;
  e.ping_rtt_ms = ping;
  e.end_to_end_rtt_ms = end_to_end;
  e.throughput_kbps = throughput_->getEstimate(now);
  e.as_of = now;

  auto combined = combineRtt(ping, tr, http, opts_.combine_bias_to_lower);
  e.rtt_ms = combined.value_or(
      http.value_or(tr.value_or(ping.value_or(0.0)))
  );
  
  // Compute effective connection type
  e.effective_type = computeEffectiveConnectionType(
      http, tr, e.throughput_kbps, opts_.ect_thresholds);
  
  // Apply throughput clamping based on ECT (Chrome NQE-style)
  clampThroughputBasedOnEct(&e.throughput_kbps, e.effective_type);
  
  return e;
}

Estimate Nqe::getEstimate(TimePoint now) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  auto e = combineLocked(now);
  if (cb_) cb_(e);
  return e;
}

void Nqe::setEstimateCallback(EstimateCallback cb) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  cb_ = std::move(cb);
}

void Nqe::registerTcpSocket(SocketHandle sock) {
  sampler_->addSocket(sock);
  Logger::instance().debug("Registered TCP socket: ", static_cast<int>(sock));
}

void Nqe::unregisterTcpSocket(SocketHandle sock) {
  sampler_->removeSocket(sock);
  Logger::instance().debug("Unregistered TCP socket: ", static_cast<int>(sock));
}

void Nqe::startTransportSampler() { 
  sampler_->start(); 
  Logger::instance().info("Transport sampler started");
}

void Nqe::stopTransportSampler()  { 
  sampler_->stop();
  Logger::instance().info("Transport sampler stopped");
}

Statistics Nqe::getStatistics() const {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  auto now = Clock::now();
  
  Statistics stats;
  
  // HTTP stats
  stats.http.sample_count = http_->sampleCount();
  stats.http.min_ms = http_->minValue();
  stats.http.max_ms = http_->maxValue();
  stats.http.percentile_50th = http_->percentile(now, 0.50);
  stats.http.percentile_95th = http_->percentile(now, 0.95);
  stats.http.percentile_99th = http_->percentile(now, 0.99);
  stats.http.last_sample_time = http_->latestTs();
  
  // Transport stats
  stats.transport.sample_count = transport_->sampleCount();
  stats.transport.min_ms = transport_->minValue();
  stats.transport.max_ms = transport_->maxValue();
  stats.transport.percentile_50th = transport_->percentile(now, 0.50);
  stats.transport.percentile_95th = transport_->percentile(now, 0.95);
  stats.transport.percentile_99th = transport_->percentile(now, 0.99);
  stats.transport.last_sample_time = transport_->latestTs();
  
  // Ping stats
  stats.ping.sample_count = ping_->sampleCount();
  stats.ping.min_ms = ping_->minValue();
  stats.ping.max_ms = ping_->maxValue();
  stats.ping.percentile_50th = ping_->percentile(now, 0.50);
  stats.ping.percentile_95th = ping_->percentile(now, 0.95);
  stats.ping.percentile_99th = ping_->percentile(now, 0.99);
  stats.ping.last_sample_time = ping_->latestTs();
  
  // End-to-End stats
  stats.end_to_end.sample_count = end_to_end_->sampleCount();
  stats.end_to_end.min_ms = end_to_end_->minValue();
  stats.end_to_end.max_ms = end_to_end_->maxValue();
  stats.end_to_end.percentile_50th = end_to_end_->percentile(now, 0.50);
  stats.end_to_end.percentile_95th = end_to_end_->percentile(now, 0.95);
  stats.end_to_end.percentile_99th = end_to_end_->percentile(now, 0.99);
  stats.end_to_end.last_sample_time = end_to_end_->latestTs();
  
  // Throughput stats
  auto tp_stats = throughput_->getStatistics(now);
  stats.throughput.sample_count = tp_stats.sample_count;
  stats.throughput.min_kbps = tp_stats.min_kbps;
  stats.throughput.max_kbps = tp_stats.max_kbps;
  stats.throughput.percentile_50th = tp_stats.percentile_50th;
  stats.throughput.percentile_95th = tp_stats.percentile_95th;
  stats.throughput.percentile_99th = tp_stats.percentile_99th;
  stats.throughput.last_sample_time = tp_stats.last_sample_time;
  
  stats.total_samples = stats.http.sample_count + stats.transport.sample_count + 
                        stats.ping.sample_count + stats.end_to_end.sample_count + 
                        stats.throughput.sample_count;
  stats.active_sockets = sampler_->socketCount();
  
  // Compute current ECT
  auto http_rtt = http_->estimate(now);
  auto transport_rtt = transport_->estimate(now);
  auto throughput = throughput_->getEstimate(now);
  stats.effective_type = computeEffectiveConnectionType(
      http_rtt, transport_rtt, throughput, opts_.ect_thresholds);
  
  return stats;
}

size_t Nqe::getActiveSockets() const {
  return sampler_->socketCount();
}

bool Nqe::isTransportSamplerRunning() const {
  return sampler_->isRunning();
}

bool Nqe::validateOptions(const Options& opts, std::string* error_msg) {
  if (opts.decay_lambda_per_sec < 0) {
    if (error_msg) *error_msg = "decay_lambda_per_sec must be non-negative";
    Logger::instance().error("Invalid option: decay_lambda_per_sec < 0");
    return false;
  }
  if (opts.transport_sample_period.count() <= 0) {
    if (error_msg) *error_msg = "transport_sample_period must be positive";
    Logger::instance().error("Invalid option: transport_sample_period <= 0");
    return false;
  }
  if (opts.combine_bias_to_lower < 0 || opts.combine_bias_to_lower > 1) {
    if (error_msg) *error_msg = "combine_bias_to_lower must be between 0 and 1";
    Logger::instance().error("Invalid option: combine_bias_to_lower out of range [0,1]");
    return false;
  }
  if (opts.freshness_threshold.count() < 0) {
    if (error_msg) *error_msg = "freshness_threshold must be non-negative";
    Logger::instance().error("Invalid option: freshness_threshold < 0");
    return false;
  }
  return true;
}

void Nqe::addThroughputSample(size_t bytes, TimePoint start_time, TimePoint end_time) {
  {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    throughput_observations_since_last_ect_computation_++;
  }
  
  throughput_->addTransfer(bytes, start_time, end_time);
  
  // Notify observers with the calculated throughput
  double throughput_kbps = calculateThroughputKbps(bytes, start_time, end_time);
  if (throughput_kbps > 0) {
    notifyThroughputObservers(throughput_kbps);
  }
  
  // Maybe recompute ECT based on new observation
  maybeComputeEffectiveConnectionType(end_time);
}

EffectiveConnectionType Nqe::getEffectiveConnectionType(TimePoint now) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  auto http_rtt = http_->estimate(now);
  auto transport_rtt = transport_->estimate(now);
  auto throughput = throughput_->getEstimate(now);
  return computeEffectiveConnectionType(http_rtt, transport_rtt, throughput, opts_.ect_thresholds);
}

void Nqe::addRTTObserver(RTTObserver* observer) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  rtt_observers_.push_back(observer);
}

void Nqe::removeRTTObserver(RTTObserver* observer) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  auto it = std::find(rtt_observers_.begin(), rtt_observers_.end(), observer);
  if (it != rtt_observers_.end()) {
    rtt_observers_.erase(it);
  }
}

void Nqe::addThroughputObserver(ThroughputObserver* observer) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  throughput_observers_.push_back(observer);
}

void Nqe::removeThroughputObserver(ThroughputObserver* observer) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  auto it = std::find(throughput_observers_.begin(), throughput_observers_.end(), observer);
  if (it != throughput_observers_.end()) {
    throughput_observers_.erase(it);
  }
}

void Nqe::addEffectiveConnectionTypeObserver(EffectiveConnectionTypeObserver* observer) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  ect_observers_.push_back(observer);
}

void Nqe::removeEffectiveConnectionTypeObserver(EffectiveConnectionTypeObserver* observer) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  auto it = std::find(ect_observers_.begin(), ect_observers_.end(), observer);
  if (it != ect_observers_.end()) {
    ect_observers_.erase(it);
  }
}

void Nqe::notifyRTTObservers(double rtt_ms, const char* source) {
  std::vector<RTTObserver*> observers;
  {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    observers = rtt_observers_;
  }
  
  for (auto* observer : observers) {
    observer->onRTTObservation(rtt_ms, source);
  }
}

void Nqe::notifyThroughputObservers(double throughput_kbps) {
  std::vector<ThroughputObserver*> observers;
  {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    observers = throughput_observers_;
  }
  
  for (auto* observer : observers) {
    observer->onThroughputObservation(throughput_kbps);
  }
}

void Nqe::updateEffectiveConnectionType(EffectiveConnectionType new_ect) {
  std::vector<EffectiveConnectionTypeObserver*> observers;
  EffectiveConnectionType old_ect;
  
  {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    old_ect = last_ect_;
    if (old_ect == new_ect) {
      return; // No change
    }
    last_ect_ = new_ect;
    observers = ect_observers_;
  }
  
  Logger::instance().info("Effective connection type changed: ",
                          effectiveConnectionTypeToString(old_ect), " -> ",
                          effectiveConnectionTypeToString(new_ect));
  
  for (auto* observer : observers) {
    observer->onEffectiveConnectionTypeChanged(old_ect, new_ect);
  }
}

void Nqe::enableNetworkChangeDetection(bool clear_on_change) {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  
  if (network_change_detection_enabled_) {
    Logger::instance().warning("Network change detection already enabled");
    return;
  }
  
#ifndef NQE_DISABLE_NETWORK_CHANGE_NOTIFIER
  clear_on_network_change_ = clear_on_change;
  
  // Create observer if not exists
  if (!network_observer_) {
    network_observer_ = std::make_unique<NetworkChangeObserverImpl>(this);
  }
  
  // Register with the global notifier
  NetworkChangeNotifier::instance().addObserver(network_observer_.get());
  
  // Start monitoring if not already started
  if (!NetworkChangeNotifier::instance().isMonitoring()) {
    NetworkChangeNotifier::instance().start();
  }
  
  network_change_detection_enabled_ = true;
  
  Logger::instance().info("Network change detection enabled (clear_on_change=",
                          clear_on_change ? "true" : "false", ")");
#else
  Logger::instance().warning("Network change detection not available in this build");
#endif
}

void Nqe::disableNetworkChangeDetection() {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  
  if (!network_change_detection_enabled_) {
    return;
  }
  
#ifndef NQE_DISABLE_NETWORK_CHANGE_NOTIFIER
  if (network_observer_) {
    NetworkChangeNotifier::instance().removeObserver(network_observer_.get());
  }
  
  network_change_detection_enabled_ = false;
  
  Logger::instance().info("Network change detection disabled");
#endif
}

bool Nqe::isNetworkChangeDetectionEnabled() const {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  return network_change_detection_enabled_;
}

void Nqe::onNetworkChange() {
  std::vector<EffectiveConnectionTypeObserver*> observers;
  EffectiveConnectionType old_ect, new_ect;
  bool ect_changed = false;
  bool should_load_cache = false;
  
  {
    std::unique_lock<std::recursive_mutex> lk(mu_);
    
    // Mark that network has changed since last ECT computation
    network_change_since_last_ect_computation_ = true;
    
    if (clear_on_network_change_) {
      Logger::instance().info("Clearing all samples due to network change");
      clearAllSamples();
    } else {
      Logger::instance().info("Network changed, keeping existing samples");
    }
    
    should_load_cache = opts_.enable_caching;
    
    // Trigger ECT recomputation
    auto now = Clock::now();
    if (shouldComputeEffectiveConnectionType(now)) {
      old_ect = last_ect_;
      computeEffectiveConnectionTypeLocked(now);
      new_ect = last_ect_;
      
      if (new_ect != old_ect) {
        ect_changed = true;
        observers = ect_observers_;
      }
    }
  }
  
  // Load cached quality for the new network (outside lock)
  if (should_load_cache) {
    loadCachedQualityForCurrentNetwork();
  }
  
  // Notify observers and update cache outside the lock
  if (ect_changed) {
    notifyECTChangeAndUpdateCache(old_ect, new_ect, observers);
  }
}

void Nqe::clearAllSamples() {
  // Clear all aggregators by recreating them, preserving signal strength weighting config
  if (opts_.enable_signal_strength_weighting) {
    http_.reset(new Aggregator(opts_.decay_lambda_per_sec, opts_.weight_multiplier_per_signal_level));
    transport_.reset(new Aggregator(opts_.decay_lambda_per_sec, opts_.weight_multiplier_per_signal_level));
    ping_.reset(new Aggregator(opts_.decay_lambda_per_sec, opts_.weight_multiplier_per_signal_level));
    end_to_end_.reset(new Aggregator(opts_.decay_lambda_per_sec, opts_.weight_multiplier_per_signal_level));
  } else {
    http_.reset(new Aggregator(opts_.decay_lambda_per_sec));
    transport_.reset(new Aggregator(opts_.decay_lambda_per_sec));
    ping_.reset(new Aggregator(opts_.decay_lambda_per_sec));
    end_to_end_.reset(new Aggregator(opts_.decay_lambda_per_sec));
  }
  
  // Clear throughput analyzer
  ThroughputAnalyzer::Options tp_opts;
  tp_opts.decay_lambda_per_sec = opts_.decay_lambda_per_sec;
  tp_opts.freshness_threshold = opts_.freshness_threshold;
  tp_opts.min_transfer_size_bytes = opts_.min_throughput_transfer_bytes;
  tp_opts.throughput_min_requests_in_flight = opts_.throughput_min_requests_in_flight;
  throughput_.reset(new ThroughputAnalyzer(tp_opts));
  
  Logger::instance().debug("All samples cleared");
}

#ifndef NQE_DISABLE_NETWORK_CHANGE_NOTIFIER
std::optional<NetworkID> Nqe::getCurrentNetworkID() const {
  if (!network_change_detection_enabled_) {
    return std::nullopt;
  }
  
  auto& notifier = NetworkChangeNotifier::instance();
  auto conn_type = notifier.getCurrentConnectionType();
  
  // Convert ConnectionType to NetworkID::ConnectionType
  NetworkID::ConnectionType net_type;
  switch (conn_type) {
    case ConnectionType::ETHERNET:
      net_type = NetworkID::ConnectionType::ETHERNET;
      break;
    case ConnectionType::WIFI:
      net_type = NetworkID::ConnectionType::WIFI;
      break;
    case ConnectionType::CELLULAR_2G:
      net_type = NetworkID::ConnectionType::CELLULAR_2G;
      break;
    case ConnectionType::CELLULAR_3G:
      net_type = NetworkID::ConnectionType::CELLULAR_3G;
      break;
    case ConnectionType::CELLULAR_4G:
      net_type = NetworkID::ConnectionType::CELLULAR_4G;
      break;
    case ConnectionType::CELLULAR_5G:
      net_type = NetworkID::ConnectionType::CELLULAR_5G;
      break;
    case ConnectionType::BLUETOOTH:
      net_type = NetworkID::ConnectionType::BLUETOOTH;
      break;
    case ConnectionType::NONE:
      net_type = NetworkID::ConnectionType::NONE;
      break;
    default:
      net_type = NetworkID::ConnectionType::UNKNOWN;
      break;
  }
  
  // For WiFi and Cellular, we could get SSID/carrier name
  // For now, use connection type as the name
  std::string name = connectionTypeToString(conn_type);
  
  // Get signal strength if available (platform-specific)
  // For now, signal strength is not retrieved from NetworkChangeNotifier
  // Can be extended in the future
  int32_t signal_strength = -1;  // -1 means not applicable
  
  return NetworkID(net_type, name, signal_strength);
}

int32_t Nqe::getCurrentSignalStrength() const {
  std::lock_guard<std::recursive_mutex> lk(mu_);
  return current_signal_strength_;
}

#else
std::optional<NetworkID> Nqe::getCurrentNetworkID() const {
  return std::nullopt;
}

int32_t Nqe::getCurrentSignalStrength() const {
  return -1;  // Not available without network change notifier
}
#endif

void Nqe::updateCachedQuality() {
  if (!opts_.enable_caching || !cache_store_) {
    return;
  }
  
  auto network_id = getCurrentNetworkID();
  if (!network_id || !network_id->isValid()) {
    return;
  }
  
  std::lock_guard<std::recursive_mutex> lk(mu_);
  
  // Get current estimates
  auto now = Clock::now();
  auto est = combineLocked(now);
  
  // Create cached quality
  CachedNetworkQuality cached(
    est.http_ttfb_ms,
    est.transport_rtt_ms,
    est.throughput_kbps,
    est.effective_type,
    now
  );
  
  cache_store_->put(*network_id, cached);
  Logger::instance().debug("Updated cache for ", network_id->toString());
}

std::optional<Estimate> Nqe::getCachedQuality(const NetworkID& network_id) const {
  if (!opts_.enable_caching || !cache_store_) {
    return std::nullopt;
  }
  
  auto cached = cache_store_->get(network_id);
  if (!cached) {
    return std::nullopt;
  }
  
  // Convert CachedNetworkQuality to Estimate
  Estimate est;
  est.http_ttfb_ms = cached->http_rtt_ms();
  est.transport_rtt_ms = cached->transport_rtt_ms();
  est.throughput_kbps = cached->downstream_throughput_kbps();
  est.effective_type = cached->effective_type();
  est.as_of = cached->last_update_time();
  
  // Calculate combined RTT from cached values
  if (est.http_ttfb_ms && est.transport_rtt_ms) {
    auto combined = combineRtt(*est.http_ttfb_ms, *est.transport_rtt_ms, 
                               std::nullopt, opts_.combine_bias_to_lower);
    est.rtt_ms = combined.value_or((*est.http_ttfb_ms + *est.transport_rtt_ms) / 2.0);
  } else if (est.http_ttfb_ms) {
    est.rtt_ms = *est.http_ttfb_ms;
  } else if (est.transport_rtt_ms) {
    est.rtt_ms = *est.transport_rtt_ms;
  }
  
  return est;
}

void Nqe::loadCachedQualityForCurrentNetwork() {
  if (!opts_.enable_caching || !cache_store_) {
    return;
  }
  
  auto network_id = getCurrentNetworkID();
  if (!network_id || !network_id->isValid()) {
    return;
  }
  
  auto cached_est = getCachedQuality(*network_id);
  if (!cached_est) {
    Logger::instance().debug("No cached quality for ", network_id->toString());
    return;
  }
  
  std::lock_guard<std::recursive_mutex> lk(mu_);
  
  // Add cached values as initial samples
  auto now = Clock::now();
  
  if (cached_est->http_ttfb_ms) {
    http_->add(*cached_est->http_ttfb_ms, now);
    Logger::instance().info("Loaded cached HTTP RTT: ", *cached_est->http_ttfb_ms, "ms");
  }
  
  if (cached_est->transport_rtt_ms) {
    transport_->add(*cached_est->transport_rtt_ms, now);
    Logger::instance().info("Loaded cached Transport RTT: ", *cached_est->transport_rtt_ms, "ms");
  }
  
  Logger::instance().info("Loaded cached quality for ", network_id->toString());
}

bool Nqe::saveCachedData() {
  if (!opts_.enable_caching || !cache_store_) {
    return false;
  }
  
  // Update cache for current network before saving
  updateCachedQuality();
  
  return cache_store_->save();
}

bool Nqe::loadCachedData() {
  if (!opts_.enable_caching || !cache_store_) {
    return false;
  }
  
  return cache_store_->load();
}

// Chromium NQE-style ECT recomputation logic
bool Nqe::shouldComputeEffectiveConnectionType(TimePoint now) const {
  // Note: This method should be called with mutex held
  
  // 1. If sufficient time has passed since last computation
  if (last_effective_connection_type_computation_.time_since_epoch().count() > 0) {
    auto time_since_last_computation = now - last_effective_connection_type_computation_;
    if (time_since_last_computation >= opts_.effective_connection_type_recomputation_interval) {
      return true;
    }
  }
  
  // 2. If a network change has occurred since last computation
  if (network_change_since_last_ect_computation_) {
    return true;
  }
  
  // 3. If current ECT is UNKNOWN
  if (last_ect_ == EffectiveConnectionType::UNKNOWN) {
    return true;
  }
  
  // 4. If observations have increased by at least 50% since last computation
  size_t current_rtt_observations = http_->sampleCount() + 
                                     transport_->sampleCount() + 
                                     ping_->sampleCount() +
                                     end_to_end_->sampleCount();
  size_t current_throughput_observations = throughput_->getSampleCount();
  
  // Check if RTT observations increased by 50%
  if (rtt_observations_at_last_ect_computation_ > 0) {
    double rtt_increase_ratio = static_cast<double>(current_rtt_observations) / 
                                 static_cast<double>(rtt_observations_at_last_ect_computation_);
    if (rtt_increase_ratio >= 1.5) {
      return true;
    }
  }
  
  // Check if throughput observations increased by 50%
  if (throughput_observations_at_last_ect_computation_ > 0) {
    double throughput_increase_ratio = static_cast<double>(current_throughput_observations) / 
                                        static_cast<double>(throughput_observations_at_last_ect_computation_);
    if (throughput_increase_ratio >= 1.5) {
      return true;
    }
  }
  
  // 5. If minimum new observation count threshold is met
  size_t new_rtt_observations = current_rtt_observations - rtt_observations_at_last_ect_computation_;
  size_t new_throughput_observations = current_throughput_observations - 
                                        throughput_observations_at_last_ect_computation_;
  size_t total_new_observations = new_rtt_observations + new_throughput_observations;
  
  if (total_new_observations >= opts_.count_new_observations_received_compute_ect) {
    return true;
  }
  
  return false;
}

void Nqe::computeEffectiveConnectionTypeLocked(TimePoint now) {
  // Note: This method should be called with mutex held (via unique_lock)
  // The caller must pass a unique_lock that can be unlocked/locked
  
  // Get current estimates
  auto http_rtt = http_->estimate(now);
  auto transport_rtt = transport_->estimate(now);
  auto throughput = throughput_->getEstimate(now);
  
  // Compute new ECT
  EffectiveConnectionType new_ect = computeEffectiveConnectionType(
      http_rtt, transport_rtt, throughput, opts_.ect_thresholds);
  
  // Update tracking variables
  last_effective_connection_type_computation_ = now;
  rtt_observations_at_last_ect_computation_ = http_->sampleCount() + 
                                               transport_->sampleCount() + 
                                               ping_->sampleCount() +
                                               end_to_end_->sampleCount();
  throughput_observations_at_last_ect_computation_ = throughput_->getSampleCount();
  network_change_since_last_ect_computation_ = false;
  
  // Check if ECT changed
  if (new_ect == last_ect_) {
    return; // No change, nothing to notify
  }
  
  Logger::instance().info("ECT changed from ", 
                          effectiveConnectionTypeToString(last_ect_),
                          " to ", 
                          effectiveConnectionTypeToString(new_ect));
  
  EffectiveConnectionType old_ect = last_ect_;
  last_ect_ = new_ect;
  
  // Copy observers to notify them outside the lock
  std::vector<EffectiveConnectionTypeObserver*> observers = ect_observers_;
  
  // Note: Observer notifications and cache updates will be done by caller
  // after they release the lock to avoid deadlocks and maintain exception safety
}

void Nqe::notifyECTChangeAndUpdateCache(
    EffectiveConnectionType old_ect,
    EffectiveConnectionType new_ect,
    const std::vector<EffectiveConnectionTypeObserver*>& observers) {
  // This method should be called WITHOUT mutex held
  
  // Notify observers with both old and new ECT
  for (auto* observer : observers) {
    observer->onEffectiveConnectionTypeChanged(old_ect, new_ect);
  }
  
  // Update cache if needed
  if (opts_.enable_caching) {
    updateCachedQuality();
  }
}

void Nqe::maybeComputeEffectiveConnectionType(TimePoint now) {
  std::vector<EffectiveConnectionTypeObserver*> observers;
  EffectiveConnectionType old_ect, new_ect;
  bool ect_changed = false;
  
  {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    
    if (!shouldComputeEffectiveConnectionType(now)) {
      return; // No recomputation needed
    }
    
    old_ect = last_ect_;
    computeEffectiveConnectionTypeLocked(now);
    new_ect = last_ect_;
    
    if (new_ect != old_ect) {
      ect_changed = true;
      observers = ect_observers_;
    }
  }
  
  // Notify observers and update cache outside the lock
  if (ect_changed) {
    notifyECTChangeAndUpdateCache(old_ect, new_ect, observers);
  }
}

// ============================================================================
// HTTP RTT Bounding and Adjustment (Chrome NQE-style)
// ============================================================================

void Nqe::updateHttpRttUsingAllRttValues(
    std::optional<double>* http_rtt,
    const std::optional<double>& transport_rtt,
    const std::optional<double>& end_to_end_rtt,
    size_t transport_rtt_count,
    size_t end_to_end_rtt_count) const {
  
  if (!http_rtt || !(*http_rtt)) {
    return; // No HTTP RTT to bound
  }
  
  double bounded_http_rtt = **http_rtt;
  
  // Lower bound: HTTP RTT should be >= Transport RTT × multiplier
  // (if we have sufficient transport RTT samples)
  if (transport_rtt && 
      transport_rtt_count >= opts_.http_rtt_transport_rtt_min_count &&
      opts_.lower_bound_http_rtt_transport_rtt_multiplier > 0) {
    
    double lower_bound = *transport_rtt * opts_.lower_bound_http_rtt_transport_rtt_multiplier;
    if (bounded_http_rtt < lower_bound) {
      Logger::instance().debug("Bounding HTTP RTT by transport RTT: ",
                               bounded_http_rtt, "ms -> ", lower_bound, "ms");
      bounded_http_rtt = lower_bound;
    }
  }
  
  // Bounds based on end-to-end RTT (if we have sufficient samples)
  if (end_to_end_rtt && 
      end_to_end_rtt_count >= opts_.http_rtt_end_to_end_rtt_min_count) {
    
    // Lower bound: HTTP RTT >= End-to-End RTT × lower multiplier
    if (opts_.lower_bound_http_rtt_end_to_end_rtt_multiplier > 0) {
      double lower_bound = *end_to_end_rtt * opts_.lower_bound_http_rtt_end_to_end_rtt_multiplier;
      if (bounded_http_rtt < lower_bound) {
        Logger::instance().debug("Bounding HTTP RTT by end-to-end RTT (lower): ",
                                 bounded_http_rtt, "ms -> ", lower_bound, "ms");
        bounded_http_rtt = lower_bound;
      }
    }
    
    // Upper bound: HTTP RTT <= End-to-End RTT × upper multiplier
    if (opts_.upper_bound_http_rtt_end_to_end_rtt_multiplier > 0) {
      double upper_bound = *end_to_end_rtt * opts_.upper_bound_http_rtt_end_to_end_rtt_multiplier;
      if (bounded_http_rtt > upper_bound) {
        Logger::instance().debug("Bounding HTTP RTT by end-to-end RTT (upper): ",
                                 bounded_http_rtt, "ms -> ", upper_bound, "ms");
        bounded_http_rtt = upper_bound;
      }
    }
  }
  
  *http_rtt = bounded_http_rtt;
}

void Nqe::adjustHttpRttBasedOnRTTCounts(
    std::optional<double>* http_rtt,
    size_t transport_rtt_count,
    EffectiveConnectionType effective_type) const {
  
  if (!opts_.adjust_rtt_based_on_rtt_counts) {
    return; // Adjustment disabled
  }
  
  // Only adjust if:
  // 1. We don't have enough transport RTT samples
  // 2. We don't have a cached estimate (checked by caller)
  // 3. Connection is fast enough (>= 3G)
  if (transport_rtt_count >= opts_.http_rtt_transport_rtt_min_count) {
    return; // Have enough transport RTT samples
  }
  
  // Only adjust for known ECT types (SLOW_2G..TYPE_4G).
  // UNKNOWN (0) and OFFLINE (1) are intentionally excluded since we don't 
  // have meaningful typical RTT values for those states.
  // Enum order: UNKNOWN=0, OFFLINE=1, SLOW_2G=2, TYPE_2G=3, TYPE_3G=4, TYPE_4G=5.
  if (effective_type < EffectiveConnectionType::SLOW_2G || 
      effective_type > EffectiveConnectionType::TYPE_4G) {
    return; // Not in valid range for adjustment
  }
  
  // Use typical HTTP RTT for fast connections to avoid hanging GET requests
  // from skewing the estimate
  const auto& thresholds = opts_.ect_thresholds;
  std::optional<double> typical_http_rtt;
  
  switch (effective_type) {
    case EffectiveConnectionType::SLOW_2G:
      typical_http_rtt = thresholds.http_rtt_slow_2g;
      break;
    case EffectiveConnectionType::TYPE_2G:
      typical_http_rtt = thresholds.http_rtt_2g;
      break;
    case EffectiveConnectionType::TYPE_3G:
      typical_http_rtt = thresholds.http_rtt_3g;
      break;
    case EffectiveConnectionType::TYPE_4G:
      // Use 3G threshold as upper bound for 4G
      typical_http_rtt = thresholds.http_rtt_3g;
      break;
    default:
      break;
  }
  
  // If we have a typical RTT and current HTTP RTT is worse (higher),
  // use the typical value instead
  if (typical_http_rtt && http_rtt && *http_rtt) {
    if (**http_rtt > *typical_http_rtt) {
      Logger::instance().debug("Adjusting HTTP RTT based on low transport RTT count: ",
                               **http_rtt, "ms -> ", *typical_http_rtt, "ms");
      *http_rtt = *typical_http_rtt;
    }
  }
}

void Nqe::clampThroughputBasedOnEct(
    std::optional<double>* throughput_kbps,
    EffectiveConnectionType effective_type) const {
  
  if (!opts_.clamp_throughput_based_on_ect) {
    return; // Clamping disabled
  }
  
  if (!throughput_kbps || !(*throughput_kbps)) {
    return; // No throughput to clamp
  }
  
  // Only clamp for slow connections (Slow 2G through 3G)
  if (effective_type > EffectiveConnectionType::TYPE_3G) {
    return; // Fast connection, no clamping needed
  }
  
  const auto& thresholds = opts_.ect_thresholds;
  std::optional<double> typical_kbps;
  
  switch (effective_type) {
    case EffectiveConnectionType::SLOW_2G:
      typical_kbps = thresholds.downstream_throughput_slow_2g;
      break;
    case EffectiveConnectionType::TYPE_2G:
      typical_kbps = thresholds.downstream_throughput_2g;
      break;
    case EffectiveConnectionType::TYPE_3G:
      typical_kbps = thresholds.downstream_throughput_3g;
      break;
    default:
      break;
  }
  
  // Clamp throughput to typical × upper_bound_multiplier
  if (typical_kbps) {
    double upper_bound = *typical_kbps * opts_.upper_bound_typical_kbps_multiplier;
    if (**throughput_kbps > upper_bound) {
      Logger::instance().debug("Clamping throughput based on ECT: ",
                               **throughput_kbps, " kbps -> ", upper_bound, " kbps (",
                               effectiveConnectionTypeToString(effective_type), ")");
      *throughput_kbps = upper_bound;
    }
  }
}

} // namespace nqe
