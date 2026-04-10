#pragma once
#include <functional>
#include <string>
#include <sstream>
#include <chrono>
#include <iomanip>

namespace nqe {

enum class LogLevel {
  LOG_DEBUG = 0,
  LOG_INFO = 1,
  LOG_WARNING = 2,
  LOG_ERROR = 3,
  LOG_NONE = 4
};

using LogCallback = std::function<void(LogLevel level, const std::string& message)>;

class Logger {
public:
  static Logger& instance() {
    static Logger inst;
    return inst;
  }

  void setCallback(LogCallback cb) {
    callback_ = std::move(cb);
  }

  void setMinLevel(LogLevel level) {
    min_level_ = level;
  }

  LogLevel getMinLevel() const {
    return min_level_;
  }

  void log(LogLevel level, const std::string& message) {
    if (level < min_level_) return;
    if (callback_) {
      callback_(level, message);
    }
  }

  template<typename... Args>
  void debugLog(Args&&... args) {
    if (LogLevel::LOG_DEBUG < min_level_) return;
    std::ostringstream oss;
    (oss << ... << std::forward<Args>(args));
    log(LogLevel::LOG_DEBUG, oss.str());
  }

  template<typename... Args>
  void infoLog(Args&&... args) {
    if (LogLevel::LOG_INFO < min_level_) return;
    std::ostringstream oss;
    (oss << ... << std::forward<Args>(args));
    log(LogLevel::LOG_INFO, oss.str());
  }

  template<typename... Args>
  void warningLog(Args&&... args) {
    if (LogLevel::LOG_WARNING < min_level_) return;
    std::ostringstream oss;
    (oss << ... << std::forward<Args>(args));
    log(LogLevel::LOG_WARNING, oss.str());
  }

  template<typename... Args>
  void errorLog(Args&&... args) {
    if (LogLevel::LOG_ERROR < min_level_) return;
    std::ostringstream oss;
    (oss << ... << std::forward<Args>(args));
    log(LogLevel::LOG_ERROR, oss.str());
  }

  // Shorter aliases for convenience
  template<typename... Args>
  void debug(Args&&... args) { debugLog(std::forward<Args>(args)...); }
  
  template<typename... Args>
  void info(Args&&... args) { infoLog(std::forward<Args>(args)...); }
  
  template<typename... Args>
  void warning(Args&&... args) { warningLog(std::forward<Args>(args)...); }
  
  template<typename... Args>
  void error(Args&&... args) { errorLog(std::forward<Args>(args)...); }

private:
  Logger() = default;
  LogLevel min_level_ = LogLevel::LOG_NONE;
  LogCallback callback_;
};

// Helper function to get a timestamp string
inline std::string getTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()) % 1000;
  
  std::ostringstream oss;
  oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
      << '.' << std::setfill('0') << std::setw(3) << ms.count();
  return oss.str();
}

// Helper to convert LogLevel to string
inline const char* logLevelToString(LogLevel level) {
  switch (level) {
    case LogLevel::LOG_DEBUG:   return "DEBUG";
    case LogLevel::LOG_INFO:    return "INFO";
    case LogLevel::LOG_WARNING: return "WARNING";
    case LogLevel::LOG_ERROR:   return "ERROR";
    case LogLevel::LOG_NONE:    return "NONE";
    default: return "UNKNOWN";
  }
}

} // namespace nqe
