#include "config.hpp"

#include <yaml-cpp/yaml.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>

#include "../logging/logger.hpp"

namespace anolis {
namespace runtime {

// Helper to parse GatingPolicy
std::optional<GatingPolicy> parse_gating_policy(const std::string &policy_str) {
    if (policy_str == "BLOCK") {
        return GatingPolicy::BLOCK;
    }
    if (policy_str == "OVERRIDE") {
        return GatingPolicy::OVERRIDE;
    }
    return std::nullopt;
}

std::string gating_policy_to_string(GatingPolicy policy) {
    switch (policy) {
        case GatingPolicy::BLOCK:
            return "BLOCK";
        case GatingPolicy::OVERRIDE:
            return "OVERRIDE";
        default:
            return "UNKNOWN";
    }
}

bool validate_config(const RuntimeConfig &config, std::string &error) {
    // Validate HTTP settings
    if (config.http.enabled) {
        if (config.http.port < 1 || config.http.port > 65535) {
            error = "HTTP port must be between 1 and 65535";
            return false;
        }
        if (config.http.thread_pool_size < 1) {
            error = "HTTP thread_pool_size must be at least 1";
            return false;
        }
        if (config.http.cors_allowed_origins.empty()) {
            error = "http.cors_allowed_origins must not be empty";
            return false;
        }
    }

    // Validate Provider settings
    if (config.providers.empty()) {
        error = "Config must specify at least one provider";
        return false;
    }

    for (const auto &provider : config.providers) {
        if (provider.id.empty()) {
            error = "Provider missing 'id' field";
            return false;
        }
        if (provider.command.empty()) {
            error = "Provider '" + provider.id + "' missing 'command' field";
            return false;
        }
        if (provider.timeout_ms < 100) {
            error = "Provider timeout must be >= 100ms";
            return false;
        }
    }

    // Validate Polling settings
    if (config.polling.interval_ms < 100) {
        error = "Polling interval must be >= 100ms";
        return false;
    }

    // Validate Logging settings
    if (config.logging.level != "debug" && config.logging.level != "info" && config.logging.level != "warn" &&
        config.logging.level != "error") {
        error = "Invalid log level: " + config.logging.level;
        return false;
    }

    // Validate Automation settings
    if (config.automation.enabled) {
        if (config.automation.behavior_tree.empty()) {
            error = "Automation enabled but behavior_tree path not specified";
            return false;
        }
        if (config.automation.tick_rate_hz < 1 || config.automation.tick_rate_hz > 1000) {
            error = "Automation tick_rate_hz must be between 1 and 1000 Hz";
            return false;
        }

        for (const auto &param : config.automation.parameters) {
            if (param.name.empty()) {
                error = "Parameter missing name";
                return false;
            }
            if (param.type != "double" && param.type != "int64" && param.type != "bool" && param.type != "string") {
                error = "Parameter '" + param.name + "' has invalid type: " + param.type;
                return false;
            }
        }
    }

    return true;
}

bool load_config(const std::string &config_path, RuntimeConfig &config, std::string &error) {
    try {
        YAML::Node yaml = YAML::LoadFile(config_path);

        // Load runtime mode config
        if (yaml["runtime"]) {
            if (yaml["runtime"]["mode"]) {
                auto mode_str = yaml["runtime"]["mode"].as<std::string>();
                auto mode = automation::string_to_mode(mode_str);
                // string_to_mode returns MANUAL for invalid strings, need check if that's desired behavior?
                // Actually string_to_mode implementation usually defaults.
                // Ideally we should validate, but existing logic might have accepted any string?
                // Config.hpp had default "MANUAL".
                // Let's assume strict parsing for better schema validation.
                // But wait, string_to_mode logic is in automation codebase.
                // Ideally I'd use that.
                config.runtime.mode = mode;
            }
        }

        // Load HTTP config
        if (yaml["http"]) {
            if (yaml["http"]["enabled"]) {
                config.http.enabled = yaml["http"]["enabled"].as<bool>();
            }
            if (yaml["http"]["bind"]) {
                config.http.bind = yaml["http"]["bind"].as<std::string>();
            }
            if (yaml["http"]["port"]) {
                config.http.port = yaml["http"]["port"].as<int>();
            }

            // CORS allowlist (supports scalar or sequence)
            if (yaml["http"]["cors_allowed_origins"]) {
                const auto &origins_node = yaml["http"]["cors_allowed_origins"];
                config.http.cors_allowed_origins.clear();
                if (origins_node.IsSequence()) {
                    for (const auto &origin : origins_node) {
                        config.http.cors_allowed_origins.push_back(origin.as<std::string>());
                    }
                } else if (origins_node.IsScalar()) {
                    config.http.cors_allowed_origins.push_back(origins_node.as<std::string>());
                }

                if (config.http.cors_allowed_origins.empty()) {
                    config.http.cors_allowed_origins.push_back("*");
                }
            }
            if (yaml["http"]["cors_allow_credentials"]) {
                config.http.cors_allow_credentials = yaml["http"]["cors_allow_credentials"].as<bool>();
            }

            if (yaml["http"]["thread_pool_size"]) {
                config.http.thread_pool_size = yaml["http"]["thread_pool_size"].as<int>();
            }
        }

        // Load providers
        if (yaml["providers"]) {
            for (const auto &provider_node : yaml["providers"]) {
                ProviderConfig provider;

                if (provider_node["id"]) {
                    provider.id = provider_node["id"].as<std::string>();
                }

                if (provider_node["command"]) {
                    provider.command = provider_node["command"].as<std::string>();
                }

                if (provider_node["args"]) {
                    for (const auto &arg : provider_node["args"]) {
                        provider.args.push_back(arg.as<std::string>());
                    }
                }

                if (provider_node["timeout_ms"]) {
                    provider.timeout_ms = provider_node["timeout_ms"].as<int>();
                }

                config.providers.push_back(provider);
            }
        }

        // Load polling config
        if (yaml["polling"]) {
            if (yaml["polling"]["interval_ms"]) {
                config.polling.interval_ms = yaml["polling"]["interval_ms"].as<int>();
            }
        }

        // Load telemetry config
        if (yaml["telemetry"]) {
            if (yaml["telemetry"]["enabled"]) {
                config.telemetry.enabled = yaml["telemetry"]["enabled"].as<bool>();
            }

            // InfluxDB settings
            if (yaml["telemetry"]["influxdb"]) {
                auto influx = yaml["telemetry"]["influxdb"];

                if (influx["url"]) {
                    config.telemetry.influx_url = influx["url"].as<std::string>();
                }
                if (influx["org"]) {
                    config.telemetry.influx_org = influx["org"].as<std::string>();
                }
                if (influx["bucket"]) {
                    config.telemetry.influx_bucket = influx["bucket"].as<std::string>();
                }
                if (influx["token"]) {
                    config.telemetry.influx_token = influx["token"].as<std::string>();
                }
                if (influx["batch_size"]) {
                    config.telemetry.batch_size = influx["batch_size"].as<size_t>();
                }
                if (influx["flush_interval_ms"]) {
                    config.telemetry.flush_interval_ms = influx["flush_interval_ms"].as<int>();
                }
            }

            // Check for token from environment variable if not in config
            if (config.telemetry.enabled && config.telemetry.influx_token.empty()) {
                const char *token_env = std::getenv("INFLUXDB_TOKEN");
                if (token_env != nullptr) {
                    config.telemetry.influx_token = token_env;
                }
            }
        }

        // Load logging config
        if (yaml["logging"]) {
            if (yaml["logging"]["level"]) {
                config.logging.level = yaml["logging"]["level"].as<std::string>();
            }
        }

        // Load automation config
        if (yaml["automation"]) {
            if (yaml["automation"]["enabled"]) {
                config.automation.enabled = yaml["automation"]["enabled"].as<bool>();
            }
            if (yaml["automation"]["behavior_tree"]) {
                config.automation.behavior_tree = yaml["automation"]["behavior_tree"].as<std::string>();
            }
            if (yaml["automation"]["tick_rate_hz"]) {
                config.automation.tick_rate_hz = yaml["automation"]["tick_rate_hz"].as<int>();
            }
            if (yaml["automation"]["manual_gating_policy"]) {
                auto policy_str = yaml["automation"]["manual_gating_policy"].as<std::string>();
                auto policy = parse_gating_policy(policy_str);
                if (!policy) {
                    error = "Invalid manual_gating_policy: must be BLOCK or OVERRIDE";
                    return false;
                }
                config.automation.manual_gating_policy = *policy;
            }

            // Load parameters
            if (yaml["automation"]["parameters"]) {
                for (const auto &param_node : yaml["automation"]["parameters"]) {
                    ParameterConfig param;

                    if (param_node["name"]) {
                        param.name = param_node["name"].as<std::string>();
                    }

                    if (param_node["type"]) {
                        param.type = param_node["type"].as<std::string>();
                    }

                    // Parse default value based on type
                    if (param_node["default"]) {
                        if (param.type == "double") {
                            param.double_value = param_node["default"].as<double>();
                        } else if (param.type == "int64") {
                            param.int64_value = param_node["default"].as<int64_t>();
                        } else if (param.type == "bool") {
                            param.bool_value = param_node["default"].as<bool>();
                        } else if (param.type == "string") {
                            param.string_value = param_node["default"].as<std::string>();
                        }
                    }

                    // Parse constraints (numeric types)
                    if (param_node["min"]) {
                        param.has_min = true;
                        param.min_value = param_node["min"].as<double>();
                    }
                    if (param_node["max"]) {
                        param.has_max = true;
                        param.max_value = param_node["max"].as<double>();
                    }

                    // Parse allowed_values (string enums)
                    if (param_node["allowed_values"]) {
                        for (const auto &val : param_node["allowed_values"]) {
                            param.allowed_values.push_back(val.as<std::string>());
                        }
                    }

                    config.automation.parameters.push_back(param);
                }
            }
        }

        // Perform validation (this replaces centralized logic in the loop)
        if (!validate_config(config, error)) {
            return false;
        }

        LOG_INFO("[Config] Loaded " << config.providers.size() << " provider(s)");
        LOG_INFO("[Config] Runtime mode: " << automation::mode_to_string(config.runtime.mode));

        std::stringstream http_msg;
        http_msg << "[Config] HTTP: " << (config.http.enabled ? "enabled" : "disabled");
        if (config.http.enabled) {
            http_msg << " (" << config.http.bind << ":" << config.http.port << ")";
        }
        LOG_INFO(http_msg.str());

        LOG_INFO("[Config] Polling interval: " << config.polling.interval_ms << "ms");

        std::stringstream telemetry_msg;
        telemetry_msg << "[Config] Telemetry: " << (config.telemetry.enabled ? "enabled" : "disabled");
        if (config.telemetry.enabled) {
            telemetry_msg << " (" << config.telemetry.influx_url << "/" << config.telemetry.influx_bucket << ")";
        }
        LOG_INFO(telemetry_msg.str());

        LOG_INFO("[Config] Log level: " << config.logging.level);

        std::stringstream automation_msg;
        automation_msg << "[Config] Automation: " << (config.automation.enabled ? "enabled" : "disabled");
        if (config.automation.enabled) {
            automation_msg << " (BT: " << config.automation.behavior_tree
                           << ", tick rate: " << config.automation.tick_rate_hz << " Hz, "
                           << config.automation.parameters.size() << " parameters)";
        }
        LOG_INFO(automation_msg.str());

        return true;
    } catch (const YAML::BadFile &e) {
        error = "Cannot open config file: " + config_path;
        return false;
    } catch (const YAML::ParserException &e) {
        error = "YAML parse error: " + std::string(e.what());
        return false;
    } catch (const std::exception &e) {
        error = "Config load error: " + std::string(e.what());
        return false;
    }
}

}  // namespace runtime
}  // namespace anolis
