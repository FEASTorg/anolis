#pragma once

#include <string>
#include <vector>

#include "../automation/mode_manager.hpp"

namespace anolis {
namespace runtime {

// Configuration enums
enum class GatingPolicy { BLOCK, OVERRIDE };

struct RestartPolicyConfig {
    bool enabled = false;                          // Enable automatic restart on crash
    int max_attempts = 3;                          // Max restart attempts before giving up
    std::vector<int> backoff_ms{100, 1000, 5000};  // Exponential backoff schedule (ms)
    int timeout_ms = 30000;                        // Timeout for restart attempt (30s default)
};

struct ProviderConfig {
    std::string id;                      // e.g., "sim0"
    std::string command;                 // Path to provider executable
    std::vector<std::string> args;       // Command-line arguments
    int timeout_ms = 5000;               // ADPP operation timeout (default 5s)
    RestartPolicyConfig restart_policy;  // Automatic restart configuration
};

struct PollingConfig {
    int interval_ms = 500;  // Default 500ms
};

struct LoggingConfig {
    std::string level = "info";  // debug, info, warn, error
};

struct HttpConfig {
    bool enabled = true;                                 // HTTP server enabled
    std::string bind = "127.0.0.1";                      // Bind address
    int port = 8080;                                     // HTTP port
    std::vector<std::string> cors_allowed_origins{"*"};  // CORS allowlist ("*" = allow all)
    bool cors_allow_credentials = false;                 // Whether to emit Access-Control-Allow-Credentials
    int thread_pool_size = 40;                           // Worker thread pool size
};

struct RuntimeModeConfig {
    automation::RuntimeMode mode = automation::RuntimeMode::MANUAL;  // Default mode
};

struct TelemetryConfig {
    bool enabled = false;  // Enable telemetry sink

    // InfluxDB settings
    std::string influx_url = "http://localhost:8086";  // InfluxDB URL
    std::string influx_org = "anolis";                 // InfluxDB organization
    std::string influx_bucket = "anolis";              // InfluxDB bucket
    std::string influx_token;                          // InfluxDB API token (from env)

    // Batching configuration
    size_t batch_size = 100;       // Flush when batch reaches this size
    int flush_interval_ms = 1000;  // Flush every N milliseconds

    // Queue settings
    size_t queue_size = 10000;            // Event queue size
    size_t max_retry_buffer_size = 1000;  // Max events to buffer on write failure
};

// Parameter definition
struct ParameterConfig {
    std::string name;
    std::string type;  // "double", "int64", "bool", "string"

    // Value variants (parsed based on type)
    double double_value = 0.0;
    int64_t int64_value = 0;
    bool bool_value = false;
    std::string string_value;

    // Constraints
    bool has_min = false;
    double min_value = 0.0;
    bool has_max = false;
    double max_value = 0.0;
    std::vector<std::string> allowed_values;  // For string enums
};

// Automation configuration
struct AutomationConfig {
    bool enabled = false;
    std::string behavior_tree;                                // Path to BT XML file
    int tick_rate_hz = 10;                                    // BT tick rate (1-1000 Hz)
    GatingPolicy manual_gating_policy = GatingPolicy::BLOCK;  // BLOCK or OVERRIDE
    std::vector<ParameterConfig> parameters;                  // Runtime parameters
};

struct RuntimeConfig {
    RuntimeModeConfig runtime;
    HttpConfig http;
    std::vector<ProviderConfig> providers;
    PollingConfig polling;
    TelemetryConfig telemetry;
    LoggingConfig logging;
    AutomationConfig automation;
};

// Loads configuration from a YAML file
bool load_config(const std::string &config_path, RuntimeConfig &config, std::string &error);

// Validates the configuration
bool validate_config(const RuntimeConfig &config, std::string &error);

}  // namespace runtime
}  // namespace anolis
