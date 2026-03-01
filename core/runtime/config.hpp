#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../automation/parameter_types.hpp"
#include "../provider/provider_config.hpp"

namespace anolis {
namespace runtime {

// Configuration enums
enum class GatingPolicy { BLOCK, OVERRIDE };

struct PollingConfig {
    int interval_ms = 500;  // Default 500ms
};

struct LoggingConfig {
    std::string level = "info";  // debug, info, warn, error
};

// Runtime section configuration (runtime: in YAML)
struct RuntimeModeConfig {
    std::string name;                // Instance identifier (optional, for multi-runtime deployments)
    int shutdown_timeout_ms = 2000;  // Provider graceful shutdown timeout (500-30000ms)
    int startup_timeout_ms = 30000;  // Overall startup timeout for fail-fast (5000-300000ms)
};

struct HttpConfig {
    bool enabled = true;                                 // HTTP server enabled
    std::string bind = "127.0.0.1";                      // Bind address
    int port = 8080;                                     // HTTP port
    std::vector<std::string> cors_allowed_origins{"*"};  // CORS allowlist ("*" = allow all)
    bool cors_allow_credentials = false;                 // Whether to emit Access-Control-Allow-Credentials
    int thread_pool_size = 40;                           // Worker thread pool size
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
    automation::ParameterType type = automation::ParameterType::DOUBLE;
    automation::ParameterValue default_value = 0.0;

    // Constraints
    std::optional<double> min;
    std::optional<double> max;
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
    RuntimeModeConfig runtime;  // Runtime section (IDLE mode hardcoded, not configurable)
    HttpConfig http;
    std::vector<provider::ProviderConfig> providers;
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
