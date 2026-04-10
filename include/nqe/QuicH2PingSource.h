#pragma once
#include "nqe/Nqe.h"
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace nqe {

// 适配层：由具体 H2/QUIC 实现发送 PING，PONG 到达时回调 onPong
class QuicH2PingSource {
public:
  explicit QuicH2PingSource(Nqe& nqe) : nqe_(nqe) {}

  using PingFunc = std::function<void(const std::string& authority)>;

  void setPingImpl(PingFunc f) { ping_ = std::move(f); }

  void ping(const std::string& authority) {
    if (!ping_) return;
    auto now = Clock::now();
    {
      std::lock_guard<std::mutex> lk(mu_);
      in_flight_[authority] = now;
    }
    ping_(authority);
  }

  void onPong(const std::string& authority) {
    auto now = Clock::now();
    TimePoint start{};
    {
      std::lock_guard<std::mutex> lk(mu_);
      auto it = in_flight_.find(authority);
      if (it == in_flight_.end()) return;
      start = it->second;
      in_flight_.erase(it);
    }
    auto rtt_ms = std::chrono::duration<double, std::milli>(now - start).count();
    if (rtt_ms > 0) nqe_.addSample(Source::PING_RTT, rtt_ms, now);
  }

private:
  Nqe& nqe_;
  PingFunc ping_;
  std::mutex mu_;
  std::unordered_map<std::string, TimePoint> in_flight_;
};

} // namespace nqe
