#pragma once

#include "config.hpp"
#include "provider/provider_handle.hpp"
#include "registry/device_registry.hpp"
#include "state/state_cache.hpp"
#include "control/call_router.hpp"
#include "http/server.hpp"
#include "events/event_emitter.hpp"
#include "telemetry/influx_sink.hpp"
#include "automation/bt_runtime.hpp"  // Phase 7
#include "automation/mode_manager.hpp"  // Phase 7B
#include "automation/parameter_manager.hpp"  // Phase 7C
#include <memory>
#include <unordered_map>

namespace anolis {
namespace runtime {

class Runtime {
public:
    Runtime(const RuntimeConfig& config);
    ~Runtime();

    // Initialize all components (providers, registry, state cache, HTTP)
    bool initialize(std::string& error);
    
    // Main runtime loop (blocking)
    void run();
    
    // Shutdown all providers gracefully
    void shutdown();
    
    // Access to core components (for future HTTP/BT layers)
    registry::DeviceRegistry& get_registry() { return *registry_; }
    state::StateCache& get_state_cache() { return *state_cache_; }
    control::CallRouter& get_call_router() { return *call_router_; }
    events::EventEmitter& get_event_emitter() { return *event_emitter_; }
    
    // Provider map access (for HTTP layer)
    std::unordered_map<std::string, std::shared_ptr<provider::ProviderHandle>>& get_providers() {
        return providers_;
    }
    
private:
    RuntimeConfig config_;
    
    std::unordered_map<std::string, std::shared_ptr<provider::ProviderHandle>> providers_;
    std::unique_ptr<registry::DeviceRegistry> registry_;
    std::shared_ptr<events::EventEmitter> event_emitter_;  // Shared with StateCache + HTTP
    std::unique_ptr<state::StateCache> state_cache_;
    std::unique_ptr<control::CallRouter> call_router_;
    std::unique_ptr<http::HttpServer> http_server_;
    std::unique_ptr<telemetry::InfluxSink> telemetry_sink_;  // Phase 6B
    std::unique_ptr<automation::ModeManager> mode_manager_;   // Phase 7B
    std::unique_ptr<automation::ParameterManager> parameter_manager_;  // Phase 7C
    std::unique_ptr<automation::BTRuntime> bt_runtime_;      // Phase 7
    
    bool running_ = false;
};

} // namespace runtime
} // namespace anolis
