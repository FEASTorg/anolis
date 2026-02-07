#include "state_cache.hpp"

#include <cstring>
#include <thread>

#include "events/event_emitter.hpp"
#include "logging/logger.hpp"

namespace anolis {
namespace state {

// CachedSignalValue methods
bool CachedSignalValue::is_stale(std::chrono::milliseconds timeout, std::chrono::system_clock::time_point now) const {
    // Check quality-based staleness
    if (quality == anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_STALE ||
        quality == anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_FAULT ||
        quality == anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_UNKNOWN) {
        return true;
    }

    // Time-based staleness
    auto age_ms = age(now);
    return age_ms > timeout;
}

std::chrono::milliseconds CachedSignalValue::age(std::chrono::system_clock::time_point now) const {
    if (now < timestamp) {
        return std::chrono::milliseconds(0);
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - timestamp);
}

// StateCache implementation
StateCache::StateCache(const registry::DeviceRegistry &registry, int poll_interval_ms)
    : registry_(registry), polling_active_(false), poll_interval_(poll_interval_ms), event_emitter_(nullptr) {}

StateCache::~StateCache() { stop_polling(); }

void StateCache::set_event_emitter(const std::shared_ptr<events::EventEmitter> &emitter) { event_emitter_ = emitter; }

bool StateCache::initialize() {
    // Build polling configuration from registry
    auto all_devices = registry_.get_all_devices();  // Returns vector<RegisteredDevice> by value

    for (const auto &device : all_devices) {
        PollConfig config;
        config.provider_id = device.provider_id;
        config.device_id = device.device_id;

        // Collect default signals (is_default = true)
        for (const auto &[signal_id, spec] : device.capabilities.signals_by_id) {
            if (spec.is_default) {
                config.signal_ids.push_back(signal_id);
            }
        }

        if (!config.signal_ids.empty()) {
            poll_configs_.push_back(config);

            // Initialize empty device state
            DeviceState state;
            state.device_handle = device.get_handle();
            state.provider_available = true;
            state.last_poll_time = std::chrono::system_clock::now();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                device_states_[state.device_handle] = state;
            }
        }
    }
    return true;
}

void StateCache::rebuild_poll_configs(const std::string &provider_id) {
    LOG_INFO("[StateCache] Rebuilding poll configs for provider: " << provider_id);

    // Remove old poll configs for this provider
    poll_configs_.erase(
        std::remove_if(poll_configs_.begin(), poll_configs_.end(),
                       [&provider_id](const PollConfig &config) { return config.provider_id == provider_id; }),
        poll_configs_.end());

    // Remove old device states for this provider
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = device_states_.begin(); it != device_states_.end();) {
            // Check if device_handle starts with "provider_id/"
            if (it->first.find(provider_id + "/") == 0) {
                it = device_states_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Rebuild poll configs from registry for this provider
    auto all_devices = registry_.get_all_devices();
    size_t new_device_count = 0;

    for (const auto &device : all_devices) {
        // Only process devices from the specified provider
        if (device.provider_id != provider_id) {
            continue;
        }

        PollConfig config;
        config.provider_id = device.provider_id;
        config.device_id = device.device_id;

        // Collect default signals (is_default = true)
        for (const auto &[signal_id, spec] : device.capabilities.signals_by_id) {
            if (spec.is_default) {
                config.signal_ids.push_back(signal_id);
            }
        }

        if (!config.signal_ids.empty()) {
            poll_configs_.push_back(config);
            new_device_count++;

            // Initialize empty device state
            DeviceState state;
            state.device_handle = device.get_handle();
            state.provider_available = true;
            state.last_poll_time = std::chrono::system_clock::now();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                device_states_[state.device_handle] = state;
            }
        }
    }

    LOG_INFO("[StateCache] Rebuilt poll configs for " << new_device_count << " devices from provider " << provider_id);
}

void StateCache::start_polling(provider::ProviderRegistry &provider_registry) {
    if (polling_active_) {
        LOG_WARN("[StateCache] Polling already active");
        return;
    }

    polling_active_ = true;
    LOG_INFO("[StateCache] Polling thread starting");

    polling_thread_ = std::thread([this, &provider_registry]() {
        while (polling_active_) {
            auto poll_start = std::chrono::steady_clock::now();

            poll_once(provider_registry);

            // Sleep until next poll interval
            auto poll_duration = std::chrono::steady_clock::now() - poll_start;
            auto sleep_time = poll_interval_ - std::chrono::duration_cast<std::chrono::milliseconds>(poll_duration);

            if (sleep_time.count() > 0) {
                std::this_thread::sleep_for(sleep_time);
            } else {
                LOG_WARN("[StateCache] Poll took longer than interval ("
                         << std::chrono::duration_cast<std::chrono::milliseconds>(poll_duration).count() << "ms)");
            }
        }
        LOG_INFO("[StateCache] Polling thread exited");
    });
}

void StateCache::stop_polling() {
    polling_active_ = false;
    if (polling_thread_.joinable()) {
        polling_thread_.join();
    }
}

void StateCache::poll_once(provider::ProviderRegistry &provider_registry) {
    for (const auto &config : poll_configs_) {
        // Get provider handle
        auto provider = provider_registry.get_provider(config.provider_id);
        if (!provider) {
            LOG_WARN("[StateCache] Provider " << config.provider_id << " not found");
            continue;
        }

        if (!provider->is_available()) {
            LOG_WARN("[StateCache] Provider " << config.provider_id << " not available");

            // Mark device state as unavailable
            std::string handle = config.provider_id + "/" + config.device_id;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto state_it = device_states_.find(handle);
                if (state_it != device_states_.end()) {
                    state_it->second.provider_available = false;
                }
            }
            continue;
        }

        // Poll device
        poll_device(config.provider_id, config.device_id, config.signal_ids, *provider);
    }
}

void StateCache::poll_device_now(const std::string &device_handle, provider::ProviderRegistry &provider_registry) {
    // Find poll config for this device
    for (const auto &config : poll_configs_) {
        std::string handle = config.provider_id + "/" + config.device_id;
        if (handle == device_handle) {
            auto provider = provider_registry.get_provider(config.provider_id);
            if (provider && provider->is_available()) {
                poll_device(config.provider_id, config.device_id, config.signal_ids, *provider);
            }
            return;
        }
    }

    LOG_WARN("[StateCache] No poll config found for " << device_handle);
}

bool StateCache::poll_device(const std::string &provider_id, const std::string &device_id,
                             const std::vector<std::string> &signal_ids, provider::IProviderHandle &provider) {
    std::string device_handle = provider_id + "/" + device_id;

    // Call ReadSignals
    anolis::deviceprovider::v0::ReadSignalsResponse response;
    if (!provider.read_signals(device_id, signal_ids, response)) {
        LOG_ERROR("[StateCache] ReadSignals failed for " << device_id << ": " << provider.last_error());

        // Mark device as unavailable and clear signals
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = device_states_.find(device_handle);
            if (it != device_states_.end()) {
                it->second.provider_available = false;
                it->second.signals.clear();
            }
        }

        return false;
    }

    // Update cache (with change detection and event emission)
    update_device_state(device_handle, provider_id, device_id, response);

    return true;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void StateCache::update_device_state(const std::string &device_handle, const std::string &provider_id,
                                     const std::string &device_id,
                                     const anolis::deviceprovider::v0::ReadSignalsResponse &response) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = device_states_.find(device_handle);
    if (it == device_states_.end()) {
        LOG_WARN("[StateCache] Device state not found: " << device_handle);
        return;
    }

    auto &state = it->second;
    state.last_poll_time = std::chrono::system_clock::now();
    state.provider_available = true;

    // Update signal values with change detection
    for (const auto &signal_value : response.values()) {
        const std::string &signal_id = signal_value.signal_id();
        const auto &new_value = signal_value.value();
        auto new_quality = signal_value.quality();

        // Check for existing value
        auto sig_it = state.signals.find(signal_id);
        bool is_new = (sig_it == state.signals.end());

        // Detect changes
        bool val_changed = is_new || value_changed(sig_it->second.value, new_value);
        bool qual_changed = is_new || quality_changed(sig_it->second.quality, new_quality);

        // Update cached value
        CachedSignalValue cached;
        cached.value = new_value;
        cached.quality = new_quality;

        if (signal_value.has_timestamp()) {
            const auto &proto_ts = signal_value.timestamp();
            auto duration = std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::seconds(proto_ts.seconds()) + std::chrono::nanoseconds(proto_ts.nanos()));
            cached.timestamp = std::chrono::system_clock::time_point(duration);
        } else {
            cached.timestamp = std::chrono::system_clock::now();
        }

        state.signals[signal_id] = cached;

        // Emit event if value or quality changed
        if (val_changed || qual_changed) {
            emit_state_update(provider_id, device_id, signal_id, new_value, new_quality);
        }
    }
}

bool StateCache::value_changed(const anolis::deviceprovider::v0::Value &old_val,
                               const anolis::deviceprovider::v0::Value &new_val) const {
    // Different types = changed
    if (old_val.type() != new_val.type()) {
        return true;
    }

    using VT = anolis::deviceprovider::v0::ValueType;
    switch (old_val.type()) {
        case VT::VALUE_TYPE_DOUBLE: {
            // Bitwise comparison for deterministic NaN/+/-0 handling
            double old_d = old_val.double_value();
            double new_d = new_val.double_value();
            uint64_t old_bits, new_bits;
            std::memcpy(&old_bits, &old_d, sizeof(double));
            std::memcpy(&new_bits, &new_d, sizeof(double));
            return old_bits != new_bits;
        }
        case VT::VALUE_TYPE_INT64:
            return old_val.int64_value() != new_val.int64_value();
        case VT::VALUE_TYPE_UINT64:
            return old_val.uint64_value() != new_val.uint64_value();
        case VT::VALUE_TYPE_BOOL:
            return old_val.bool_value() != new_val.bool_value();
        case VT::VALUE_TYPE_STRING:
            return old_val.string_value() != new_val.string_value();
        case VT::VALUE_TYPE_BYTES:
            return old_val.bytes_value() != new_val.bytes_value();
        default:
            // Unknown type - assume changed
            return true;
    }
}

bool StateCache::quality_changed(anolis::deviceprovider::v0::SignalValue_Quality old_q,
                                 anolis::deviceprovider::v0::SignalValue_Quality new_q) const {
    return old_q != new_q;
}

void StateCache::emit_state_update(const std::string &provider_id, const std::string &device_id,
                                   const std::string &signal_id, const anolis::deviceprovider::v0::Value &value,
                                   anolis::deviceprovider::v0::SignalValue_Quality quality) {
    if (!event_emitter_) {
        return;  // No emitter attached, skip
    }

    // Convert protobuf Value to TypedValue
    events::TypedValue typed_val;
    using VT = anolis::deviceprovider::v0::ValueType;
    switch (value.type()) {
        case VT::VALUE_TYPE_DOUBLE:
            typed_val = value.double_value();
            break;
        case VT::VALUE_TYPE_INT64:
            typed_val = value.int64_value();
            break;
        case VT::VALUE_TYPE_UINT64:
            typed_val = value.uint64_value();
            break;
        case VT::VALUE_TYPE_BOOL:
            typed_val = value.bool_value();
            break;
        case VT::VALUE_TYPE_STRING:
            typed_val = value.string_value();
            break;
        case VT::VALUE_TYPE_BYTES:
            typed_val = value.bytes_value();
            break;
        default:
            typed_val = std::string("<unknown>");
            break;
    }

    // Convert protobuf quality to event quality
    events::Quality event_quality;
    using Q = anolis::deviceprovider::v0::SignalValue_Quality;
    switch (quality) {
        case Q::SignalValue_Quality_QUALITY_OK:
            event_quality = events::Quality::OK;
            break;
        case Q::SignalValue_Quality_QUALITY_STALE:
            event_quality = events::Quality::STALE;
            break;
        case Q::SignalValue_Quality_QUALITY_FAULT:
            event_quality = events::Quality::FAULT;
            break;
        default:
            event_quality = events::Quality::UNAVAILABLE;
            break;
    }

    // Create and emit event (event_id assigned by emitter)
    auto event = events::StateUpdateEvent::create(0,  // event_id assigned by emitter
                                                  provider_id, device_id, signal_id, typed_val, event_quality);

    event_emitter_->emit(event);
}

std::shared_ptr<DeviceState> StateCache::get_device_state(const std::string &device_handle) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = device_states_.find(device_handle);
    if (it == device_states_.end()) {
        LOG_DEBUG("[StateCache] get_device_state: " << device_handle << " not found");
        return nullptr;
    }

    LOG_DEBUG("[StateCache] get_device_state: " << device_handle << " found (signals=" << it->second.signals.size()
                                                << ")");
    // Return copy as shared_ptr for thread safety
    return std::make_shared<DeviceState>(it->second);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::shared_ptr<CachedSignalValue> StateCache::get_signal_value(const std::string &device_handle,
                                                                const std::string &signal_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
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
    std::lock_guard<std::mutex> lock(mutex_);
    return device_states_.size();
}

}  // namespace state
}  // namespace anolis
