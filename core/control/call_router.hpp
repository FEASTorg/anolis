#ifndef ANOLIS_CONTROL_CALL_ROUTER_HPP
#define ANOLIS_CONTROL_CALL_ROUTER_HPP

#include <string>
#include <map>
#include <memory>
#include <mutex>
#include "protocol.pb.h"
#include "registry/device_registry.hpp"
#include "provider/provider_handle.hpp"
#include "state/state_cache.hpp"

namespace anolis {

// Forward declarations
namespace automation { class ModeManager; }

namespace control {

// Call request - High-level API for executing device functions
struct CallRequest {
    std::string device_handle;  // "provider_id/device_id"
    std::string function_name;
    std::map<std::string, anolis::deviceprovider::v0::Value> args;
};

// Call result - Status and optional return values
struct CallResult {
    bool success;
    std::string error_message;
    std::map<std::string, anolis::deviceprovider::v0::Value> results;
};

// CallRouter - Unified control path with validation and state management
class CallRouter {
public:
    CallRouter(const registry::DeviceRegistry& registry,
              state::StateCache& state_cache);
    
    /**
     * Set mode manager for manual/auto gating (Phase 7B.2).
     * Must be called before execute_call if automation is enabled.
     */
    void set_mode_manager(automation::ModeManager* mode_manager, const std::string& gating_policy);
    
    // Execute a function call
    // This is the ONLY way to execute control actions in Anolis
    CallResult execute_call(const CallRequest& request,
                           std::unordered_map<std::string, std::shared_ptr<provider::ProviderHandle>>& providers);
    
    // Validation only (no execution)
    bool validate_call(const CallRequest& request, std::string& error) const;
    
private:
    const registry::DeviceRegistry& registry_;
    state::StateCache& state_cache_;
    automation::ModeManager* mode_manager_ = nullptr;  // Phase 7B.2 (optional)
    std::string manual_gating_policy_ = "BLOCK";      // Phase 7B.2
    
    // Per-provider mutexes for serialized access (v0: prevent concurrent calls to same provider)
    std::map<std::string, std::mutex> provider_locks_;
    
    // Validation helpers
    bool validate_device_exists(const std::string& device_handle, std::string& error) const;
    bool validate_function_exists(const registry::RegisteredDevice& device,
                                  const std::string& function_name,
                                  const registry::FunctionSpec*& out_spec,
                                  std::string& error) const;
    bool validate_arguments(const registry::FunctionSpec& spec,
                           const std::map<std::string, anolis::deviceprovider::v0::Value>& args,
                           std::string& error) const;
    
    // Helper: Parse device_handle into provider_id and device_id
    bool parse_device_handle(const std::string& device_handle,
                            std::string& provider_id,
                            std::string& device_id,
                            std::string& error) const;
};

} // namespace control
} // namespace anolis

#endif // ANOLIS_CONTROL_CALL_ROUTER_HPP
