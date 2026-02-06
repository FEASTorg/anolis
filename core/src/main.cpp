// Anolis Runtime
// Config-based runtime with CLI argument parsing

#include <iostream>
#include <string>
#include <filesystem>
#include "runtime/runtime.hpp"
#include "runtime/config.hpp"

int main(int argc, char **argv)
{
    std::cerr << "===========================================\n";
    std::cerr << "  Anolis Core Runtime v0 \n";
    std::cerr << "===========================================\n\n";

    // Parse CLI arguments
    std::string config_path = "anolis-runtime.yaml"; // Default

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--config" && i + 1 < argc)
        {
            config_path = argv[++i];
        }
        else if (arg.substr(0, 9) == "--config=")
        {
            config_path = arg.substr(9);
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cerr << "Usage: anolis-runtime [OPTIONS]\n\n";
            std::cerr << "Options:\n";
            std::cerr << "  --config=PATH    Path to config file (default: anolis-runtime.yaml)\n";
            std::cerr << "  --help, -h       Show this help\n";
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << "\n";
            std::cerr << "Use --help for usage information\n";
            return 1;
        }
    }

    // Check if config exists
    if (!std::filesystem::exists(config_path))
    {
        std::cerr << "ERROR: Config file not found: " << config_path << "\n";
        std::cerr << "\nCreate a config file or specify path with --config=PATH\n";
        return 1;
    }

    std::cerr << "[Main] Loading config: " << config_path << "\n\n";

    // Load configuration
    anolis::runtime::RuntimeConfig config;
    std::string error;

    if (!anolis::runtime::load_config(config_path, config, error))
    {
        std::cerr << "ERROR: Failed to load config: " << error << "\n";
        return 1;
    }

    std::cerr << "\n";

    // Create and initialize runtime
    anolis::runtime::Runtime runtime(config);

    if (!runtime.initialize(error))
    {
        std::cerr << "ERROR: Runtime initialization failed: " << error << "\n";
        return 1;
    }

    std::cerr << "\n";
    std::cerr << "===========================================\n";
    std::cerr << "  Runtime Ready\n";
    std::cerr << "===========================================\n";
    std::cerr << "  Providers: " << config.providers.size() << "\n";
    std::cerr << "  Devices: " << runtime.get_registry().device_count() << "\n";
    std::cerr << "  Polling: " << config.polling.interval_ms << "ms\n";
    std::cerr << "===========================================\n\n";

    // Run main loop (blocking)
    runtime.run();

    std::cerr << "\n[Main] Shutdown complete\n";
    return 0;
}
