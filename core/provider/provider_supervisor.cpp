#include "provider_supervisor.hpp"

#include "../logging/logger.hpp"

namespace anolis {
namespace provider {

void ProviderSupervisor::register_provider(const std::string &provider_id, const runtime::RestartPolicyConfig &policy) {
    policies_[provider_id] = policy;
    states_[provider_id] = RestartState{};

    if (policy.enabled) {
        LOG_INFO("[Supervisor] Registered provider '"
                 << provider_id << "' with restart policy (max_attempts=" << policy.max_attempts << ")");
    } else {
        LOG_DEBUG("[Supervisor] Registered provider '" << provider_id << "' without restart policy");
    }
}

bool ProviderSupervisor::should_restart(const std::string &provider_id) const {
    auto policy_it = policies_.find(provider_id);
    auto state_it = states_.find(provider_id);

    if (policy_it == policies_.end() || state_it == states_.end()) {
        return false;  // Unknown provider
    }

    const auto &policy = policy_it->second;
    const auto &state = state_it->second;

    // Check if restart policy is enabled
    if (!policy.enabled) {
        return false;
    }

    // Check if circuit breaker is open
    if (state.circuit_open) {
        return false;
    }

    // Check if backoff period has elapsed
    auto now = std::chrono::steady_clock::now();
    if (now < state.next_restart_time) {
        return false;
    }

    return true;
}

int ProviderSupervisor::get_backoff_ms(const std::string &provider_id) const {
    auto policy_it = policies_.find(provider_id);
    auto state_it = states_.find(provider_id);

    if (policy_it == policies_.end() || state_it == states_.end()) {
        return 0;
    }

    const auto &policy = policy_it->second;
    const auto &state = state_it->second;

    // If no attempts made yet or circuit open, no backoff
    if (state.attempt_count == 0 || state.circuit_open) {
        return 0;
    }

    // Get backoff for previous attempt (array is 0-indexed, attempts are 1-indexed)
    int attempt_index = state.attempt_count - 1;
    if (attempt_index >= 0 && attempt_index < static_cast<int>(policy.backoff_ms.size())) {
        return policy.backoff_ms[attempt_index];
    }

    return 0;
}

bool ProviderSupervisor::record_crash(const std::string &provider_id) {
    auto policy_it = policies_.find(provider_id);
    auto state_it = states_.find(provider_id);

    if (policy_it == policies_.end() || state_it == states_.end()) {
        return false;
    }

    const auto &policy = policy_it->second;
    auto &state = state_it->second;

    // If restart policy is disabled, circuit opens immediately
    if (!policy.enabled) {
        state.circuit_open = true;
        LOG_INFO("[Supervisor] Provider '" << provider_id << "' crashed (restart policy disabled)");
        return false;
    }

    // Increment attempt count
    state.attempt_count++;

    // Check if max attempts exceeded
    if (state.attempt_count > policy.max_attempts) {
        state.circuit_open = true;
        LOG_ERROR("[Supervisor] Provider '" << provider_id << "' crashed (circuit breaker open, exceeded "
                                            << policy.max_attempts << " restart attempts)");
        return false;
    }

    // Calculate backoff delay for this attempt
    int attempt_index = state.attempt_count - 1;
    int backoff_ms = policy.backoff_ms[attempt_index];

    // Schedule next restart time
    auto now = std::chrono::steady_clock::now();
    state.next_restart_time = now + std::chrono::milliseconds(backoff_ms);

    LOG_WARN("[Supervisor] Provider '" << provider_id << "' crashed (attempt " << state.attempt_count << "/"
                                       << policy.max_attempts << ", retry in " << backoff_ms << "ms)");

    return true;
}

void ProviderSupervisor::record_success(const std::string &provider_id) {
    auto state_it = states_.find(provider_id);

    if (state_it == states_.end()) {
        return;
    }

    auto &state = state_it->second;

    // If this was a recovery from crash, log it
    if (state.attempt_count > 0) {
        LOG_INFO("[Supervisor] Provider '" << provider_id << "' recovered successfully (after " << state.attempt_count
                                           << " restart attempts)");
    }

    // Reset state
    state.attempt_count = 0;
    state.circuit_open = false;
    state.next_restart_time = std::chrono::steady_clock::time_point{};
}

bool ProviderSupervisor::is_circuit_open(const std::string &provider_id) const {
    auto state_it = states_.find(provider_id);

    if (state_it == states_.end()) {
        return false;
    }

    return state_it->second.circuit_open;
}

int ProviderSupervisor::get_attempt_count(const std::string &provider_id) const {
    auto state_it = states_.find(provider_id);

    if (state_it == states_.end()) {
        return 0;
    }

    return state_it->second.attempt_count;
}

}  // namespace provider
}  // namespace anolis
