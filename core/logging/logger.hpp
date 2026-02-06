#pragma once

#include <string>
#include <mutex>
#include <sstream>

namespace anolis {
namespace logging {

enum class Level {
    LVL_DEBUG,
    LVL_INFO,
    LVL_WARN,
    LVL_ERROR,
    LVL_NONE
};

class Logger {
public:
    static void init(Level threshold);
    static void log(Level level, const char* file, int line, const std::string& message);
    static void set_level(Level level);

private:
    static Level threshold_;
    static std::mutex mutex_;
};

// Helper to convert Level to string for config parsing
Level string_to_level(const std::string& level_str);

} // namespace logging
} // namespace anolis

// Macro macros to handle string building
#define LOG_INTERNAL(level, msg) \
    do { \
        std::stringstream ss; \
        ss << msg; \
        anolis::logging::Logger::log(level, __FILE__, __LINE__, ss.str()); \
    } while(0)

#define LOG_DEBUG(msg) LOG_INTERNAL(anolis::logging::Level::LVL_DEBUG, msg)
#define LOG_INFO(msg)  LOG_INTERNAL(anolis::logging::Level::LVL_INFO, msg)
#define LOG_WARN(msg)  LOG_INTERNAL(anolis::logging::Level::LVL_WARN, msg)
#define LOG_ERROR(msg) LOG_INTERNAL(anolis::logging::Level::LVL_ERROR, msg)
