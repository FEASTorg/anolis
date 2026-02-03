#pragma once

#include <string>
#include <vector>

namespace anolis {
namespace runtime {

struct ProviderConfig {
    std::string id;           // e.g., "sim0"
    std::string command;      // Path to provider executable
    std::vector<std::string> args;  // Command-line arguments
    int timeout_ms = 5000;    // ADPP operation timeout (default 5s)
};

struct PollingConfig {
    int interval_ms = 500;    // Default 500ms
};

struct LoggingConfig {
    std::string level = "info";  // debug, info, warn, error
};

struct HttpConfig {
    bool enabled = true;         // HTTP server enabled
    std::string bind = "127.0.0.1";  // Bind address
    int port = 8080;             // HTTP port
};

struct RuntimeModeConfig {
    std::string mode = "MANUAL";  // Always MANUAL in v0 (IDLE, AUTO, FAULT in Phase 6)
};

struct TelemetryConfig {
    bool enabled = false;        // Phase 5 (TSDB sink)
};

struct RuntimeConfig {
    RuntimeModeConfig runtime;
    HttpConfig http;
    std::vector<ProviderConfig> providers;
    PollingConfig polling;
    TelemetryConfig telemetry;
    LoggingConfig logging;
};

// Load config from YAML file
bool load_config(const std::string& config_path, RuntimeConfig& config, std::string& error);

} // namespace runtime
} // namespace anolis
