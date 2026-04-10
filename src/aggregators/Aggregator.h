#pragma once
#include "WeightedMedian.h"
#include <optional>

namespace nqe {

/// Shared Aggregator wrapping WeightedMedian with min/max/sample-count tracking.
/// Used by both Nqe and ThroughputAnalyzer to avoid duplicate definitions.
class Aggregator {
public:
  explicit Aggregator(double lambda) : wm_(lambda) {}
  explicit Aggregator(double lambda, double signal_mult) : wm_(lambda, signal_mult) {}
  
  void add(double v, TimePoint ts) { 
    wm_.add(v, ts); 
    latest_ = ts; 
    sample_count_++;
    if (!min_value_ || v < *min_value_) min_value_ = v;
    if (!max_value_ || v > *max_value_) max_value_ = v;
  }
  
  void add(double v, TimePoint ts, int32_t signal_strength) { 
    wm_.add(v, ts, signal_strength); 
    latest_ = ts; 
    sample_count_++;
    if (!min_value_ || v < *min_value_) min_value_ = v;
    if (!max_value_ || v > *max_value_) max_value_ = v;
  }
  
  std::optional<double> estimate(TimePoint now) const { return wm_.estimate(now); }
  std::optional<double> estimate(TimePoint now, int32_t signal_strength) const { 
    return wm_.estimate(now, signal_strength); 
  }
  TimePoint latestTs() const { return latest_; }
  size_t sampleCount() const { return sample_count_; }
  std::optional<double> minValue() const { return min_value_; }
  std::optional<double> maxValue() const { return max_value_; }
  
  std::optional<double> percentile(TimePoint now, double p) const {
    return wm_.percentile(now, p);
  }
  
  std::optional<double> percentile(TimePoint now, double p, int32_t signal_strength) const {
    return wm_.percentile(now, p, signal_strength);
  }
  
private:
  WeightedMedian wm_;
  TimePoint latest_{};
  size_t sample_count_ = 0;
  std::optional<double> min_value_;
  std::optional<double> max_value_;
};

} // namespace nqe
