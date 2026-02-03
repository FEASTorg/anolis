#include "state_cache.hpp"
#include <iostream>
#include <thread>

namespace anolis {
namespace state {

// CachedSignalValue methods
bool CachedSignalValue::is_stale() const {
    // Check quality-based staleness
    if (quality == anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_STALE ||
        quality == anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_FAULT ||
        quality == anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_UNKNOWN) {
        return true;
    }
    
    // Time-based staleness: stale if older than 2 seconds (2x poll interval)
    auto age_ms = age();
    return age_ms.count() > 2000;
}

std::chrono::milliseconds CachedSignalValue::age() const {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - timestamp);
}

// StateCache implementation
StateCache::StateCache(const registry::DeviceRegistry& registry, int poll_interval_ms)
    : registry_(registry), polling_active_(false), poll_interval_(poll_interval_ms) {}

bool StateCache::initialize() {
    std::cerr << "[StateCache] Initializing state cache\n";
    
    // Build polling configuration from registry
    auto all_devices = registry_.get_all_devices();
    
    for (const auto* device : all_devices) {
        PollConfig config;
        config.provider_id = device->provider_id;
        config.device_id = device->device_id;
        
        // Collect default signals (is_default = true)
        for (const auto& [signal_id, spec] : device->capabilities.signals_by_id) {
            if (spec.is_default) {
                config.signal_ids.push_back(signal_id);
            }
        }
        
        if (!config.signal_ids.empty()) {
            poll_configs_.push_back(config);
            
            // Initialize empty device state
            DeviceState state;
            state.device_handle = device->get_handle();
            state.provider_available = true;
            state.last_poll_time = std::chrono::system_clock::now();
            device_states_[state.device_handle] = state;
            
            std::cerr << "[StateCache] Will poll " << state.device_handle 
                      << " (" << config.signal_ids.size() << " signals)\n";
        }
    }
    
    std::cerr << "[StateCache] Initialized " << poll_configs_.size() << " poll configs\n";
    return true;
}

void StateCache::start_polling(std::unordered_map<std::string, std::shared_ptr<provider::ProviderHandle>>& providers) {
    polling_active_ = true;
    std::cerr << "[StateCache] Polling started (interval=" << poll_interval_.count() << "ms)\n";
    
    // v0: Simple blocking loop in main thread
    // Phase 3B will move this to separate thread
    while (polling_active_) {
        auto poll_start = std::chrono::steady_clock::now();
        
        poll_once(providers);
        
        // Sleep until next poll interval
        auto poll_duration = std::chrono::steady_clock::now() - poll_start;
        auto sleep_time = poll_interval_ - std::chrono::duration_cast<std::chrono::milliseconds>(poll_duration);
        
        if (sleep_time.count() > 0) {
            std::this_thread::sleep_for(sleep_time);
        } else {
            std::cerr << "[StateCache] WARNING: Poll took longer than interval ("
                      << std::chrono::duration_cast<std::chrono::milliseconds>(poll_duration).count() 
                      << "ms)\n";
        }
    }
    
    std::cerr << "[StateCache] Polling stopped\n";
}

void StateCache::stop_polling() {
    polling_active_ = false;
}

void StateCache::poll_once(std::unordered_map<std::string, std::shared_ptr<provider::ProviderHandle>>& providers) {
    for (const auto& config : poll_configs_) {
        // Get provider handle
        auto it = providers.find(config.provider_id);
        if (it == providers.end()) {
            std::cerr << "[StateCache] WARNING: Provider " << config.provider_id << " not found\n";
            continue;
        }
        
        auto& provider = it->second;
        if (!provider->is_available()) {
            std::cerr << "[StateCache] WARNING: Provider " << config.provider_id << " not available\n";
            
            // Mark device state as unavailable
            std::string handle = config.provider_id + "/" + config.device_id;
            auto state_it = device_states_.find(handle);
            if (state_it != device_states_.end()) {
                state_it->second.provider_available = false;
            }
            continue;
        }
        
        // Poll device
        poll_device(config.provider_id, config.device_id, config.signal_ids, *provider);
    }
}

void StateCache::poll_device_now(const std::string& device_handle,
                                 std::unordered_map<std::string, std::shared_ptr<provider::ProviderHandle>>& providers) {
    std::cerr << "[StateCache] Immediate poll requested for " << device_handle << "\n";
    
    // Find poll config for this device
    for (const auto& config : poll_configs_) {
        std::string handle = config.provider_id + "/" + config.device_id;
        if (handle == device_handle) {
            auto it = providers.find(config.provider_id);
            if (it != providers.end() && it->second->is_available()) {
                poll_device(config.provider_id, config.device_id, config.signal_ids, *it->second);
            }
            return;
        }
    }
    
    std::cerr << "[StateCache] WARNING: No poll config found for " << device_handle << "\n";
}

bool StateCache::poll_device(const std::string& provider_id,
                             const std::string& device_id,
                             const std::vector<std::string>& signal_ids,
                             provider::ProviderHandle& provider) {
    // Call ReadSignals
    anolis::deviceprovider::v0::ReadSignalsResponse response;
    if (!provider.read_signals(device_id, signal_ids, response)) {
        std::cerr << "[StateCache] ReadSignals failed for " << device_id 
                  << ": " << provider.last_error() << "\n";
        return false;
    }
    
    // Update cache
    std::string device_handle = provider_id + "/" + device_id;
    update_device_state(device_handle, response);
    
    return true;
}

void StateCache::update_device_state(const std::string& device_handle,
                                     const anolis::deviceprovider::v0::ReadSignalsResponse& response) {
    auto it = device_states_.find(device_handle);
    if (it == device_states_.end()) {
        std::cerr << "[StateCache] WARNING: Device state not found: " << device_handle << "\n";
        return;
    }
    
    auto& state = it->second;
    state.last_poll_time = std::chrono::system_clock::now();
    state.provider_available = true;
    
    // Update signal values
    for (const auto& signal_value : response.values()) {
        CachedSignalValue cached;
        cached.value = signal_value.value();
        cached.quality = signal_value.quality();
        
        // Convert protobuf timestamp to system_clock
        if (signal_value.has_timestamp()) {
            auto proto_ts = signal_value.timestamp();
            auto duration = std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::seconds(proto_ts.seconds()) + 
                std::chrono::nanoseconds(proto_ts.nanos()));
            cached.timestamp = std::chrono::system_clock::time_point(duration);
        } else {
            cached.timestamp = std::chrono::system_clock::now();
        }
        
        state.signals[signal_value.signal_id()] = cached;
    }
}

std::shared_ptr<DeviceState> StateCache::get_device_state(const std::string& device_handle) const {
    auto it = device_states_.find(device_handle);
    if (it == device_states_.end()) {
        return nullptr;
    }
    
    // Return copy as shared_ptr for thread safety
    return std::make_shared<DeviceState>(it->second);
}

std::shared_ptr<CachedSignalValue> StateCache::get_signal_value(const std::string& device_handle,
                                                                 const std::string& signal_id) const {
    auto it = device_states_.find(device_handle);
    if (it == device_states_.end()) {
        return nullptr;
    }
    
    auto sig_it = it->second.signals.find(signal_id);
    if (sig_it == it->second.signals.end()) {
        return nullptr;
    }
    
    // Return copy as shared_ptr for thread safety
    return std::make_shared<CachedSignalValue>(sig_it->second);
}

size_t StateCache::device_count() const {
    return device_states_.size();
}

} // namespace state
} // namespace anolis
