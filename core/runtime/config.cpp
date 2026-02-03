#include "config.hpp"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <iostream>

namespace anolis {
namespace runtime {

bool load_config(const std::string& config_path, RuntimeConfig& config, std::string& error) {
    try {
        YAML::Node yaml = YAML::LoadFile(config_path);
        
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
        std::cerr << "[Config] Polling interval: " << config.polling.interval_ms << "ms\n";
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
