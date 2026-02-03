#ifndef ANOLIS_STATE_STATE_CACHE_HPP
#define ANOLIS_STATE_STATE_CACHE_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <memory>
#include "protocol.pb.h"
#include "registry/device_registry.hpp"
#include "provider/provider_handle.hpp"

namespace anolis {
namespace state {

// Cached signal value with metadata
struct CachedSignalValue {
    anolis::deviceprovider::v0::Value value;
    std::chrono::system_clock::time_point timestamp;
    anolis::deviceprovider::v0::SignalValue_Quality quality;
    
    // Staleness: true if time-based or quality-based staleness detected
    bool is_stale() const;
    
    // Time since last update
    std::chrono::milliseconds age() const;
};

// Device state snapshot
struct DeviceState {
    std::string device_handle;  // "provider_id/device_id"
    std::unordered_map<std::string, CachedSignalValue> signals;  // signal_id -> value
    std::chrono::system_clock::time_point last_poll_time;
    bool provider_available;
};

// State Cache - Single source of truth for device state
class StateCache {
public:
    StateCache(const registry::DeviceRegistry& registry, int poll_interval_ms = 500);
    
    // Initialize: Build polling lists from registry
    bool initialize();
    
    // Start polling thread (v0: runs in main thread)
    void start_polling(std::unordered_map<std::string, std::shared_ptr<provider::ProviderHandle>>& providers);
    
    // Stop polling
    void stop_polling();
    
    // Poll once (for testing or manual control)
    void poll_once(std::unordered_map<std::string, std::shared_ptr<provider::ProviderHandle>>& providers);
    
    // Read API - Thread-safe snapshots
    std::shared_ptr<DeviceState> get_device_state(const std::string& device_handle) const;
    std::shared_ptr<CachedSignalValue> get_signal_value(const std::string& device_handle,
                                                         const std::string& signal_id) const;
    
    // Immediate poll of specific device (post-call update)
    void poll_device_now(const std::string& device_handle,
                        std::unordered_map<std::string, std::shared_ptr<provider::ProviderHandle>>& providers);
    
    // Status
    size_t device_count() const;
    const std::string& last_error() const { return error_; }
    
private:
    const registry::DeviceRegistry& registry_;
    std::string error_;
    
    // Cached state (indexed by device_handle)
    std::unordered_map<std::string, DeviceState> device_states_;
    
    // Polling configuration (built from registry at init)
    struct PollConfig {
        std::string provider_id;
        std::string device_id;
        std::vector<std::string> signal_ids;  // Default signals only
    };
    std::vector<PollConfig> poll_configs_;
    
    // Polling control
    bool polling_active_;
    std::chrono::milliseconds poll_interval_;
    
    // Helper: Poll single device
    bool poll_device(const std::string& provider_id,
                    const std::string& device_id,
                    const std::vector<std::string>& signal_ids,
                    provider::ProviderHandle& provider);
    
    // Helper: Update cached values from ReadSignalsResponse
    void update_device_state(const std::string& device_handle,
                            const anolis::deviceprovider::v0::ReadSignalsResponse& response);
};

} // namespace state
} // namespace anolis

#endif // ANOLIS_STATE_STATE_CACHE_HPP
