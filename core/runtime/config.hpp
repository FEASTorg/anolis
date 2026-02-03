#pragma once

#include <string>
#include <vector>

namespace anolis {
namespace runtime {

struct ProviderConfig {
    std::string id;           // e.g., "sim0"
    std::string command;      // Path to provider executable
    std::vector<std::string> args;  // Command-line arguments
};

struct PollingConfig {
    int interval_ms = 500;    // Default 500ms
};

struct LoggingConfig {
    std::string level = "info";  // debug, info, warn, error
};

struct RuntimeConfig {
    std::vector<ProviderConfig> providers;
    PollingConfig polling;
    LoggingConfig logging;
};

// Load config from YAML file
bool load_config(const std::string& config_path, RuntimeConfig& config, std::string& error);

} // namespace runtime
} // namespace anolis
