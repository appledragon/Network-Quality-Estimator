#pragma once
#include "nqe/Nqe.h"
#include <mutex>
#include <unordered_map>

namespace nqe {

// 通用 Hook：在“请求发送”与“收到响应头”时调用，内部计算 TTFB（上界）
class HttpRttSource {
public:
  explicit HttpRttSource(Nqe& nqe) : nqe_(nqe) {}

  void onRequestSent(uint64_t request_id, nqe::TimePoint ts = nqe::Clock::now()) {
    std::lock_guard<std::mutex> lk(mu_);
    send_ts_[request_id] = ts;
  }

  void onResponseHeaders(uint64_t request_id, nqe::TimePoint ts = nqe::Clock::now()) {
    nqe::TimePoint send{};
    {
      std::lock_guard<std::mutex> lk(mu_);
      auto it = send_ts_.find(request_id);
      if (it == send_ts_.end()) return;
      send = it->second;
      send_ts_.erase(it);
    }
    auto ttfb = std::chrono::duration<double, std::milli>(ts - send).count();
    if (ttfb > 0) nqe_.addSample(Source::HTTP_TTFB, ttfb, ts);
  }

private:
  Nqe& nqe_;
  std::mutex mu_;
  std::unordered_map<uint64_t, nqe::TimePoint> send_ts_;

};

} // namespace nqe
