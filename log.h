#pragma once
#include <iostream>
#include <atomic>

enum class LogLevel { NONE = 0, ERROR = 1, WARN = 2, INFO = 3, DEBUG = 4 };

inline std::atomic<LogLevel>& globalLogLevel() {
    static std::atomic<LogLevel> level{LogLevel::INFO};
    return level;
}

inline void setLogLevel(LogLevel level) { globalLogLevel().store(level); }
inline LogLevel getLogLevel() { return globalLogLevel().load(); }

// Parse from string (for config/cmdline)
inline LogLevel parseLogLevel(const std::string& s) {
    if (s == "none")  return LogLevel::NONE;
    if (s == "error") return LogLevel::ERROR;
    if (s == "warn")  return LogLevel::WARN;
    if (s == "info")  return LogLevel::INFO;
    if (s == "debug") return LogLevel::DEBUG;
    return LogLevel::INFO;
}

#define LOG_ERROR(msg) do { if (getLogLevel() >= LogLevel::ERROR) std::cerr << msg << std::endl; } while(0)
#define LOG_WARN(msg)  do { if (getLogLevel() >= LogLevel::WARN)  std::cerr << msg << std::endl; } while(0)
#define LOG_INFO(msg)  do { if (getLogLevel() >= LogLevel::INFO)  std::cout << msg << std::endl; } while(0)
#define LOG_DEBUG(msg) do { if (getLogLevel() >= LogLevel::DEBUG) std::cout << msg << std::endl; } while(0)
