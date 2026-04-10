#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <optional>

namespace nqe {

// 时间加权中位数：w = exp(-lambda * age_sec)，近期样本权重更高
// Chrome NQE enhancement: Dual-factor weighting (time × signal strength)
class WeightedMedian {
public:
  struct Sample {
    double value_ms;
    std::chrono::steady_clock::time_point ts;
    int32_t signal_strength = -1;  ///< Signal strength (0-100), -1 if not applicable
  };

  explicit WeightedMedian(double decay_lambda_per_sec) 
    : lambda_(decay_lambda_per_sec), 
      weight_multiplier_per_signal_level_(0.98) {}
  
  explicit WeightedMedian(double decay_lambda_per_sec, double weight_mult_per_signal)
    : lambda_(decay_lambda_per_sec),
      weight_multiplier_per_signal_level_(weight_mult_per_signal) {}

  void add(double value_ms, std::chrono::steady_clock::time_point ts) {
    samples_.push_back({value_ms, ts, -1});
    if (samples_.size() > kMaxKeep) {
      samples_.pop_front();
    }
  }
  
  void add(double value_ms, std::chrono::steady_clock::time_point ts, int32_t signal_strength) {
    samples_.push_back({value_ms, ts, signal_strength});
    if (samples_.size() > kMaxKeep) {
      samples_.pop_front();
    }
  }

  std::optional<double> estimate(std::chrono::steady_clock::time_point now) const {
    return estimate(now, -1);
  }
  
  std::optional<double> estimate(std::chrono::steady_clock::time_point now, int32_t current_signal_strength) const {
    if (samples_.empty()) return std::nullopt;
    struct W { double v; double w; };
    std::vector<W> arr;
    arr.reserve(samples_.size());
    for (auto& s : samples_) {
      double age = std::chrono::duration<double>(now - s.ts).count();
      double time_weight = age < 0 ? 0.0 : std::exp(-lambda_ * age);
      
      // Apply signal strength weighting (Chrome NQE-style)
      double signal_weight = 1.0;
      if (current_signal_strength >= 0 && s.signal_strength >= 0) {
        int32_t signal_diff = std::abs(current_signal_strength - s.signal_strength);
        signal_weight = std::pow(weight_multiplier_per_signal_level_, signal_diff);
      }
      
      double w = time_weight * signal_weight;
      if (w <= 0) continue;
      arr.push_back({s.value_ms, w});
    }
    if (arr.empty()) return std::nullopt;
    std::sort(arr.begin(), arr.end(), [](const W& a, const W& b){ return a.v < b.v; });
    double total = 0; for (auto& x : arr) total += x.w;
    double half = total * 0.5;
    double cum = 0;
    for (auto& x : arr) {
      cum += x.w;
      if (cum >= half) return x.v;
    }
    return arr.back().v;
  }

  bool empty() const { return samples_.empty(); }
  std::chrono::steady_clock::time_point latestTs() const {
    std::chrono::steady_clock::time_point t{};
    for (auto& s : samples_) if (s.ts > t) t = s.ts;
    return t;
  }

  // Calculate weighted percentile (p = 0.5 for median, 0.95 for 95th percentile, etc.)
  std::optional<double> percentile(std::chrono::steady_clock::time_point now, double p) const {
    return percentile(now, p, -1);
  }
  
  std::optional<double> percentile(std::chrono::steady_clock::time_point now, double p, int32_t current_signal_strength) const {
    if (samples_.empty() || p < 0 || p > 1) return std::nullopt;
    struct W { double v; double w; };
    std::vector<W> arr;
    arr.reserve(samples_.size());
    for (auto& s : samples_) {
      double age = std::chrono::duration<double>(now - s.ts).count();
      double time_weight = age < 0 ? 0.0 : std::exp(-lambda_ * age);
      
      // Apply signal strength weighting (Chrome NQE-style)
      double signal_weight = 1.0;
      if (current_signal_strength >= 0 && s.signal_strength >= 0) {
        int32_t signal_diff = std::abs(current_signal_strength - s.signal_strength);
        signal_weight = std::pow(weight_multiplier_per_signal_level_, signal_diff);
      }
      
      double w = time_weight * signal_weight;
      if (w <= 0) continue;
      arr.push_back({s.value_ms, w});
    }
    if (arr.empty()) return std::nullopt;
    std::sort(arr.begin(), arr.end(), [](const W& a, const W& b){ return a.v < b.v; });
    double total = 0; 
    for (auto& x : arr) total += x.w;
    double target = total * p;
    double cum = 0;
    for (auto& x : arr) {
      cum += x.w;
      if (cum >= target) return x.v;
    }
    return arr.back().v;
  }

private:
  static constexpr size_t kMaxKeep = 512;
  double lambda_;
  double weight_multiplier_per_signal_level_;  ///< Weight multiplier per signal strength level difference
  std::deque<Sample> samples_;
};

} // namespace nqe
