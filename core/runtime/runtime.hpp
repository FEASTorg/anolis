#pragma once

#include <atomic>
#include <memory>
#include <unordered_map>

#include "automation/bt_runtime.hpp"
#include "automation/mode_manager.hpp"
#include "automation/parameter_manager.hpp"
#include "config.hpp"
#include "control/call_router.hpp"
#include "events/event_emitter.hpp"
#include "http/server.hpp"
#include "provider/i_provider_handle.hpp"  // Changed to interface
#include "registry/device_registry.hpp"
#include "state/state_cache.hpp"
#include "telemetry/influx_sink.hpp"

namespace anolis {
namespace runtime {

class Runtime {
public:
    Runtime(const RuntimeConfig &config);
    ~Runtime();

    // Initialize all components (providers, registry, state cache, HTTP)
    bool initialize(std::string &error);

    // Main runtime loop (blocking)
    void run();

    // Triggers the main loop to exit
    void stop() { running_ = false; }

    // Shutdown all providers gracefully
    void shutdown();

    // Access to core components (for future HTTP/BT layers)
    registry::DeviceRegistry &get_registry() { return *registry_; }
    state::StateCache &get_state_cache() { return *state_cache_; }
    control::CallRouter &get_call_router() { return *call_router_; }
    events::EventEmitter &get_event_emitter() { return *event_emitter_; }

    // Provider map access (for HTTP layer)
    std::unordered_map<std::string, std::shared_ptr<provider::IProviderHandle>> &get_providers() { return providers_; }

private:
    // Staged initialization helpers
    bool init_providers(std::string &error);
    bool init_core_services(std::string &error);
    bool init_automation(std::string &error);
    bool init_http(std::string &error);
    bool init_telemetry(std::string &error);

    RuntimeConfig config_;

    std::unordered_map<std::string, std::shared_ptr<provider::IProviderHandle>> providers_;
    std::unique_ptr<registry::DeviceRegistry> registry_;
    std::shared_ptr<events::EventEmitter> event_emitter_;  // Shared with StateCache + HTTP
    std::unique_ptr<state::StateCache> state_cache_;
    std::unique_ptr<control::CallRouter> call_router_;
    std::unique_ptr<http::HttpServer> http_server_;
    std::unique_ptr<telemetry::InfluxSink> telemetry_sink_;
    std::unique_ptr<automation::ModeManager> mode_manager_;
    std::unique_ptr<automation::ParameterManager> parameter_manager_;
    std::unique_ptr<automation::BTRuntime> bt_runtime_;

    std::atomic<bool> running_{false};
};

}  // namespace runtime
}  // namespace anolis
