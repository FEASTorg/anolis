#include "runtime.hpp"
#include <iostream>
#include <thread>
#include <chrono>

namespace anolis {
namespace runtime {

Runtime::Runtime(const RuntimeConfig& config) 
    : config_(config) {}

Runtime::~Runtime() {
    shutdown();
}

bool Runtime::initialize(std::string& error) {
    std::cerr << "[Runtime] Initializing Anolis Core\n";
    
    // Create registry
    registry_ = std::make_unique<registry::DeviceRegistry>();
    
    // Start all providers and discover
    for (const auto& provider_config : config_.providers) {
        std::cerr << "[Runtime] Starting provider: " << provider_config.id << "\n";
        std::cerr << "[Runtime]   Command: " << provider_config.command << "\n";
        
        auto provider = std::make_shared<provider::ProviderHandle>(
            provider_config.id, 
            provider_config.command,
            provider_config.args
        );
        
        if (!provider->start()) {
            error = "Failed to start provider '" + provider_config.id + "': " + provider->last_error();
            return false;
        }
        
        std::cerr << "[Runtime] Provider " << provider_config.id << " started\n";
        
        // Discover devices
        if (!registry_->discover_provider(provider_config.id, *provider)) {
            error = "Discovery failed for provider '" + provider_config.id + "': " + registry_->last_error();
            return false;
        }
        
        providers_[provider_config.id] = provider;
    }
    
    std::cerr << "[Runtime] All providers started\n";
    
    // Create state cache
    state_cache_ = std::make_unique<state::StateCache>(*registry_, config_.polling.interval_ms);
    
    if (!state_cache_->initialize()) {
        error = "State cache initialization failed: " + state_cache_->last_error();
        return false;
    }
    
    // Create call router
    call_router_ = std::make_unique<control::CallRouter>(*registry_, *state_cache_);
    
    std::cerr << "[Runtime] Initialization complete\n";
    return true;
}

void Runtime::run() {
    std::cerr << "[Runtime] Starting main loop\n";
    running_ = true;
    
    // Start state cache polling
    state_cache_->start_polling(providers_);
    
    std::cerr << "[Runtime] State cache polling active\n" << std::flush;
    std::cerr << "[Runtime] Press Ctrl+C to exit\n\n" << std::flush;
    
    // Main loop (v0: just keep polling alive)
    // Future: HTTP server, BT engine, etc. will run here
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Check provider health
        for (const auto& [id, provider] : providers_) {
            if (!provider->is_available()) {
                std::cerr << "[Runtime] WARNING: Provider " << id << " unavailable\n";
            }
        }
    }
    
    std::cerr << "[Runtime] Shutting down\n";
    state_cache_->stop_polling();
}

void Runtime::shutdown() {
    if (state_cache_) {
        state_cache_->stop_polling();
    }
    
    for (auto& [id, provider] : providers_) {
        std::cerr << "[Runtime] Stopping provider: " << id << "\n";
        // ProviderHandle/ProviderProcess destructor handles cleanup
    }
    
    providers_.clear();
}

} // namespace runtime
} // namespace anolis
