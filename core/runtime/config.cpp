#include "config.hpp"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <iostream>
#include <cstdlib>

namespace anolis {
namespace runtime {

bool load_config(const std::string& config_path, RuntimeConfig& config, std::string& error) {
    try {
        YAML::Node yaml = YAML::LoadFile(config_path);
        
        // Load runtime mode config
        if (yaml["runtime"]) {
            if (yaml["runtime"]["mode"]) {
                config.runtime.mode = yaml["runtime"]["mode"].as<std::string>();
                // v0: Only MANUAL is valid
                if (config.runtime.mode != "MANUAL") {
                    std::cerr << "[Config] WARNING: Only MANUAL mode supported in v0, ignoring: " 
                              << config.runtime.mode << "\n";
                    config.runtime.mode = "MANUAL";
                }
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
                if (config.http.port < 1 || config.http.port > 65535) {
                    error = "HTTP port must be between 1 and 65535";
                    return false;
                }
            }
        }
        
        // Load providers
        if (yaml["providers"]) {
            for (const auto& provider_node : yaml["providers"]) {
                ProviderConfig provider;
                
                if (!provider_node["id"]) {
                    error = "Provider missing 'id' field";
                    return false;
                }
                provider.id = provider_node["id"].as<std::string>();
                
                if (!provider_node["command"]) {
                    error = "Provider '" + provider.id + "' missing 'command' field";
                    return false;
                }
                provider.command = provider_node["command"].as<std::string>();
                
                // Optional args
                if (provider_node["args"]) {
                    for (const auto& arg : provider_node["args"]) {
                        provider.args.push_back(arg.as<std::string>());
                    }
                }
                
                // Optional timeout
                if (provider_node["timeout_ms"]) {
                    provider.timeout_ms = provider_node["timeout_ms"].as<int>();
                    if (provider.timeout_ms < 100) {
                        error = "Provider timeout must be >= 100ms";
                        return false;
                    }
                }
                
                config.providers.push_back(provider);
            }
        }
        
        if (config.providers.empty()) {
            error = "Config must specify at least one provider";
            return false;
        }
        
        // Load polling config
        if (yaml["polling"]) {
            if (yaml["polling"]["interval_ms"]) {
                config.polling.interval_ms = yaml["polling"]["interval_ms"].as<int>();
                
                if (config.polling.interval_ms < 100) {
                    error = "Polling interval must be >= 100ms";
                    return false;
                }
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
                
                // Batching settings
                if (influx["batch_size"]) {
                    config.telemetry.batch_size = influx["batch_size"].as<size_t>();
                }
                if (influx["flush_interval_ms"]) {
                    config.telemetry.flush_interval_ms = influx["flush_interval_ms"].as<int>();
                }
            }
            
            // Check for token from environment variable if not in config
            if (config.telemetry.enabled && config.telemetry.influx_token.empty()) {
                const char* token_env = std::getenv("INFLUXDB_TOKEN");
                if (token_env) {
                    config.telemetry.influx_token = token_env;
                }
            }
        }
        
        // Load logging config
        if (yaml["logging"]) {
            if (yaml["logging"]["level"]) {
                config.logging.level = yaml["logging"]["level"].as<std::string>();
                
                // Validate log level
                if (config.logging.level != "debug" && 
                    config.logging.level != "info" && 
                    config.logging.level != "warn" && 
                    config.logging.level != "error") {
                    error = "Invalid log level: " + config.logging.level;
                    return false;
                }
            }
        }
        
        std::cerr << "[Config] Loaded " << config.providers.size() << " provider(s)\n";
        std::cerr << "[Config] Runtime mode: " << config.runtime.mode << "\n";
        std::cerr << "[Config] HTTP: " << (config.http.enabled ? "enabled" : "disabled");
        if (config.http.enabled) {
            std::cerr << " (" << config.http.bind << ":" << config.http.port << ")";
        }
        std::cerr << "\n";
        std::cerr << "[Config] Polling interval: " << config.polling.interval_ms << "ms\n";
        std::cerr << "[Config] Telemetry: " << (config.telemetry.enabled ? "enabled" : "disabled");
        if (config.telemetry.enabled) {
            std::cerr << " (" << config.telemetry.influx_url << "/" 
                      << config.telemetry.influx_bucket << ")";
        }
        std::cerr << "\n";
        std::cerr << "[Config] Log level: " << config.logging.level << "\n";
        
        return true;
        
    } catch (const YAML::BadFile& e) {
        error = "Cannot open config file: " + config_path;
        return false;
    } catch (const YAML::ParserException& e) {
        error = "YAML parse error: " + std::string(e.what());
        return false;
    } catch (const std::exception& e) {
        error = "Config load error: " + std::string(e.what());
        return false;
    }
}

} // namespace runtime
} // namespace anolis
