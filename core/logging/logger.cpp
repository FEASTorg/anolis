#include "logger.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <algorithm>

namespace anolis {
namespace logging {

Level Logger::threshold_ = Level::LVL_INFO;
std::mutex Logger::mutex_;

void Logger::init(Level threshold) {
    threshold_ = threshold;
}

void Logger::set_level(Level level) {
    threshold_ = level;
}

void Logger::log(Level level, const char* file, int line, const std::string& message) {
    if (level < threshold_) {
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif

    std::lock_guard<std::mutex> lock(mutex_);
    
    // Timestamp
    std::cerr << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    std::cerr << "." << std::setfill('0') << std::setw(3) << ms.count() << "]";

    // Level
    switch (level) {
        case Level::LVL_DEBUG: std::cerr << " [DEBUG] "; break;
        case Level::LVL_INFO:  std::cerr << " [INFO]  "; break;
        case Level::LVL_WARN:  std::cerr << " [WARN]  "; break;
        case Level::LVL_ERROR: std::cerr << " [ERROR] "; break;
        default: break;
    }

    // Message
    std::cerr << message << "\n";
    
    // Flush on error or if explicitly configured (for now just flush on error)
    if (level >= Level::LVL_ERROR) {
        std::cerr << std::flush;
    }
}

Level string_to_level(const std::string& level_str) {
    std::string s = level_str;
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    
    if (s == "DEBUG") return Level::LVL_DEBUG;
    if (s == "INFO") return Level::LVL_INFO;
    if (s == "WARN") return Level::LVL_WARN;
    if (s == "ERROR") return Level::LVL_ERROR;
    
    return Level::LVL_INFO; // Default
}

} // namespace logging
} // namespace anolis
