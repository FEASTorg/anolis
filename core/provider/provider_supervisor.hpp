#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "../runtime/config.hpp"

namespace anolis {
namespace provider {

// ProviderSupervisor manages automatic restart of crashed providers
// Implements exponential backoff and circuit breaker pattern
class ProviderSupervisor {
public:
    // Immutable snapshot of supervision state for a single provider.
    // Returned by get_snapshot / get_all_snapshots for safe cross-thread reads.
    struct ProviderSupervisionSnapshot {
        bool supervision_enabled = false;
        int attempt_count = 0;
        int max_attempts = 0;
        bool crash_detected = false;
        bool circuit_open = false;
        std::optional<int64_t> next_restart_in_ms;  // nullopt: healthy or circuit-open (see circuit_open)
        std::optional<int64_t> last_seen_ago_ms;    // nullopt if no healthy heartbeat yet
        int64_t uptime_seconds = 0;                 // 0 before first heartbeat or when unavailable
    };

    ProviderSupervisor() = default;

    // Register a provider with its restart policy
    void register_provider(const std::string &provider_id, const runtime::RestartPolicyConfig &policy);

    // Check if a crashed provider should be restarted
    // Returns true if restart is allowed and backoff period has elapsed
    bool should_restart(const std::string &provider_id) const;

    // Get the backoff delay in milliseconds for the current restart attempt
    // Returns 0 if no restart attempts have been made or max attempts exceeded
    int get_backoff_ms(const std::string &provider_id) const;

    // Record a provider crash - increments attempt count and schedules next restart
    // Returns false if max attempts exceeded (circuit breaker opens)
    bool record_crash(const std::string &provider_id);

    // Record successful restart - resets attempt count and closes circuit breaker.
    // Resets process_start_time to zero so the next record_heartbeat begins a fresh
    // uptime measurement.
    void record_success(const std::string &provider_id);

    // Check if circuit breaker is open (max attempts exceeded)
    bool is_circuit_open(const std::string &provider_id) const;

    // Get current attempt count for a provider
    int get_attempt_count(const std::string &provider_id) const;

    // Mark that we've detected the current crash (called when provider becomes unavailable)
    // Returns true if this is a new crash (not already recorded)
    bool mark_crash_detected(const std::string &provider_id);

    // Clear crash detected flag (called after successful restart attempt begins)
    void clear_crash_detected(const std::string &provider_id);

    // Record a healthy heartbeat for a provider.
    // Updates last_healthy_time every call.
    // Sets process_start_time on the first call after initial start or after record_success
    // (i.e. when process_start_time is the default zero time point).
    // Called by the runtime main loop every iteration the provider is available.
    void record_heartbeat(const std::string &provider_id);

    // Return an immutable snapshot of supervision state for one provider.
    // Returns std::nullopt if the provider is not registered.
    // Safe to call from any thread (locks internally).
    std::optional<ProviderSupervisionSnapshot> get_snapshot(const std::string &provider_id) const;

    // Return snapshots for all registered providers.
    // Safe to call from any thread (locks internally).
    std::unordered_map<std::string, ProviderSupervisionSnapshot> get_all_snapshots() const;

private:
    struct RestartState {
        int attempt_count = 0;                                    // Current restart attempt number
        bool circuit_open = false;                                // True when max attempts exceeded
        bool crash_detected = false;                              // True if we're currently handling a crash
        std::chrono::steady_clock::time_point next_restart_time;  // Earliest time for next restart
        // Set on first record_heartbeat after start/restart; reset to {} by record_success.
        std::chrono::steady_clock::time_point process_start_time;
        // Updated by record_heartbeat every healthy loop iteration.
        std::chrono::steady_clock::time_point last_healthy_time;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, runtime::RestartPolicyConfig> policies_;
    std::unordered_map<std::string, RestartState> states_;
};

}  // namespace provider
}  // namespace anolis
