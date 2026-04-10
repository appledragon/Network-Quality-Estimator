#pragma once
#include "nqe/EffectiveConnectionType.h"
#include <functional>

namespace nqe {

/**
 * Observer interfaces for Network Quality Estimator events.
 * Based on Chromium's NQE observer pattern.
 */

/**
 * Observer for RTT measurements.
 * Notified when new RTT observations are added.
 */
class RTTObserver {
public:
  virtual ~RTTObserver() = default;
  
  /**
   * Called when a new RTT observation is available.
   * 
   * @param rtt_ms RTT value in milliseconds
   * @param source Source of the RTT observation
   */
  virtual void onRTTObservation(double rtt_ms, const char* source) = 0;
};

/**
 * Observer for throughput measurements.
 * Notified when new throughput observations are added.
 */
class ThroughputObserver {
public:
  virtual ~ThroughputObserver() = default;
  
  /**
   * Called when a new throughput observation is available.
   * 
   * @param throughput_kbps Throughput value in kilobits per second
   */
  virtual void onThroughputObservation(double throughput_kbps) = 0;
};

/**
 * Observer for Effective Connection Type changes.
 * Notified when ECT changes.
 */
class EffectiveConnectionTypeObserver {
public:
  virtual ~EffectiveConnectionTypeObserver() = default;
  
  /**
   * Called when the effective connection type changes.
   * 
   * @param old_type Previous ECT value
   * @param new_type New ECT value
   */
  virtual void onEffectiveConnectionTypeChanged(
      EffectiveConnectionType old_type,
      EffectiveConnectionType new_type) = 0;
};

/**
 * Callback-based observer implementations for convenience.
 */

using RTTObserverCallback = std::function<void(double rtt_ms, const char* source)>;
using ThroughputObserverCallback = std::function<void(double throughput_kbps)>;
using EffectiveConnectionTypeObserverCallback = 
    std::function<void(EffectiveConnectionType old_type, EffectiveConnectionType new_type)>;

class RTTObserverCallbackAdapter : public RTTObserver {
public:
  explicit RTTObserverCallbackAdapter(RTTObserverCallback cb) : callback_(std::move(cb)) {}
  void onRTTObservation(double rtt_ms, const char* source) override {
    if (callback_) callback_(rtt_ms, source);
  }
private:
  RTTObserverCallback callback_;
};

class ThroughputObserverCallbackAdapter : public ThroughputObserver {
public:
  explicit ThroughputObserverCallbackAdapter(ThroughputObserverCallback cb) 
      : callback_(std::move(cb)) {}
  void onThroughputObservation(double throughput_kbps) override {
    if (callback_) callback_(throughput_kbps);
  }
private:
  ThroughputObserverCallback callback_;
};

class EffectiveConnectionTypeObserverCallbackAdapter : public EffectiveConnectionTypeObserver {
public:
  explicit EffectiveConnectionTypeObserverCallbackAdapter(
      EffectiveConnectionTypeObserverCallback cb) 
      : callback_(std::move(cb)) {}
  void onEffectiveConnectionTypeChanged(
      EffectiveConnectionType old_type,
      EffectiveConnectionType new_type) override {
    if (callback_) callback_(old_type, new_type);
  }
private:
  EffectiveConnectionTypeObserverCallback callback_;
};

} // namespace nqe
