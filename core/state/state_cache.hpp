#ifndef ANOLIS_STATE_STATE_CACHE_HPP
#define ANOLIS_STATE_STATE_CACHE_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <mutex>
#include "protocol.pb.h"
#include "registry/device_registry.hpp"
#include "provider/i_provider_handle.hpp"

// Forward declaration
namespace anolis
{
    namespace events
    {
        class EventEmitter;
    }
}

namespace anolis
{
    namespace state
    {

        // Cached signal value with metadata
        struct CachedSignalValue
        {
            anolis::deviceprovider::v0::Value value;
            std::chrono::system_clock::time_point timestamp;
            anolis::deviceprovider::v0::SignalValue_Quality quality;

            // Staleness: true if time-based or quality-based staleness detected
            // Optional 'now' parameter for testing time-based staleness
            bool is_stale(std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) const;

            // Time since last update
            std::chrono::milliseconds age(std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) const;
        };

        // Device state snapshot
        struct DeviceState
        {
            std::string device_handle;                                  // "provider_id/device_id"
            std::unordered_map<std::string, CachedSignalValue> signals; // signal_id -> value
            std::chrono::system_clock::time_point last_poll_time;
            bool provider_available;
        };

        // State Cache - Single source of truth for device state
        class StateCache
        {
        public:
            StateCache(const registry::DeviceRegistry &registry, int poll_interval_ms = 500);

            // Initialize: Build polling lists from registry
            bool initialize();

            /**
             * @brief Set event emitter for change notifications
             *
             * When set, StateCache will emit events on value/quality changes.
             * Must be called before start_polling().
             *
             * @param emitter Shared pointer to EventEmitter (can be nullptr to disable)
             */
            void set_event_emitter(std::shared_ptr<events::EventEmitter> emitter);

            // Start polling thread (v0: runs in main thread)
            void start_polling(std::unordered_map<std::string, std::shared_ptr<provider::IProviderHandle>> &providers);

            // Stop polling
            void stop_polling();

            // Poll once (for testing or manual control)
            void poll_once(std::unordered_map<std::string, std::shared_ptr<provider::IProviderHandle>> &providers);

            // Read API - Thread-safe snapshots
            std::shared_ptr<DeviceState> get_device_state(const std::string &device_handle) const;
            std::shared_ptr<CachedSignalValue> get_signal_value(const std::string &device_handle,
                                                                const std::string &signal_id) const;

            // Immediate poll of specific device (post-call update)
            void poll_device_now(const std::string &device_handle,
                                 std::unordered_map<std::string, std::shared_ptr<provider::IProviderHandle>> &providers);

            // Status
            size_t device_count() const;
            const std::string &last_error() const { return error_; }

        private:
            const registry::DeviceRegistry &registry_;
            std::string error_;

            // Event emitter for change notifications
            std::shared_ptr<events::EventEmitter> event_emitter_;

            mutable std::mutex mutex_;

            // Cached state (indexed by device_handle)
            std::unordered_map<std::string, DeviceState> device_states_;

            // Polling configuration (built from registry at init)
            struct PollConfig
            {
                std::string provider_id;
                std::string device_id;
                std::vector<std::string> signal_ids; // Default signals only
            };
            std::vector<PollConfig> poll_configs_;

            // Polling control
            bool polling_active_;
            std::chrono::milliseconds poll_interval_;

            // Helper: Poll single device
            bool poll_device(const std::string &provider_id,
                             const std::string &device_id,
                             const std::vector<std::string> &signal_ids,
                             provider::IProviderHandle &provider);

            // Helper: Update cached values from ReadSignalsResponse (emits events on change)
            void update_device_state(const std::string &device_handle,
                                     const std::string &provider_id,
                                     const std::string &device_id,
                                     const anolis::deviceprovider::v0::ReadSignalsResponse &response);

            // Helper: Check if value changed (uses bitwise comparison for doubles)
            bool value_changed(const anolis::deviceprovider::v0::Value &old_val,
                               const anolis::deviceprovider::v0::Value &new_val) const;

            // Helper: Check if quality changed
            bool quality_changed(anolis::deviceprovider::v0::SignalValue_Quality old_q,
                                 anolis::deviceprovider::v0::SignalValue_Quality new_q) const;

            // Helper: Emit state update event
            void emit_state_update(const std::string &provider_id,
                                   const std::string &device_id,
                                   const std::string &signal_id,
                                   const anolis::deviceprovider::v0::Value &value,
                                   anolis::deviceprovider::v0::SignalValue_Quality quality);
        };

    } // namespace state
} // namespace anolis

#endif // ANOLIS_STATE_STATE_CACHE_HPP
