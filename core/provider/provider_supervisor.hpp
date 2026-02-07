#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

#include "../runtime/config.hpp"

namespace anolis {
namespace provider {

// ProviderSupervisor manages automatic restart of crashed providers
// Implements exponential backoff and circuit breaker pattern
class ProviderSupervisor {
public:
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

    // Record successful restart - resets attempt count and closes circuit breaker
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

private:
    struct RestartState {
        int attempt_count = 0;                                    // Current restart attempt number
        bool circuit_open = false;                                // True when max attempts exceeded
        bool crash_detected = false;                              // True if we're currently handling a crash
        std::chrono::steady_clock::time_point next_restart_time;  // Earliest time for next restart
    };

    std::unordered_map<std::string, runtime::RestartPolicyConfig> policies_;
    std::unordered_map<std::string, RestartState> states_;
};

}  // namespace provider
}  // namespace anolis
