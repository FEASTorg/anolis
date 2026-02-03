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
    
    // Create event emitter (Phase 6)
    // Default: 100 events per subscriber queue, max 32 SSE clients
    event_emitter_ = std::make_shared<events::EventEmitter>(100, 32);
    std::cerr << "[Runtime] Event emitter created (max " << event_emitter_->max_subscribers() << " subscribers)\n";
    
    // Create state cache
    state_cache_ = std::make_unique<state::StateCache>(*registry_, config_.polling.interval_ms);
    
    // Wire event emitter to state cache
    state_cache_->set_event_emitter(event_emitter_);
    
    if (!state_cache_->initialize()) {
        error = "State cache initialization failed: " + state_cache_->last_error();
        return false;
    }
    
    // Create call router
    call_router_ = std::make_unique<control::CallRouter>(*registry_, *state_cache_);
    
    // Create and start HTTP server if enabled
    if (config_.http.enabled) {
        std::cerr << "[Runtime] Creating HTTP server\n";
        http_server_ = std::make_unique<http::HttpServer>(
            config_.http,
            *registry_,
            *state_cache_,
            *call_router_,
            providers_,
            event_emitter_  // Pass event emitter for SSE
        );
        
        std::string http_error;
        if (!http_server_->start(http_error)) {
            error = "HTTP server failed to start: " + http_error;
            return false;
        }
        std::cerr << "[Runtime] HTTP server started on " << config_.http.bind 
                  << ":" << config_.http.port << "\n";
    } else {
        std::cerr << "[Runtime] HTTP server disabled in config\n";
    }
    
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
    // Stop HTTP server first
    if (http_server_) {
        std::cerr << "[Runtime] Stopping HTTP server\n";
        http_server_->stop();
    }
    
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
