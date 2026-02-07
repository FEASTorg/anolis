#pragma once

#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "i_provider_handle.hpp"

namespace anolis {
namespace provider {

/**
 * @brief Thread-safe registry for provider handles
 *
 * ProviderRegistry wraps the provider map with std::shared_mutex to enable:
 * - Concurrent reads from multiple threads (StateCache polling, HTTP handlers, BT ticks)
 * - Exclusive writes during provider lifecycle operations (start, stop, restart)
 *
 * This is a critical concurrency primitive that prevents data races during
 * provider supervision and restart scenarios.
 *
 * Thread Safety:
 * - All methods are thread-safe
 * - get_provider() and get_all_providers() allow concurrent reads
 * - add_provider(), remove_provider(), clear() require exclusive access
 *
 * Design Constraints (Phase 13):
 * - Maintains shared_ptr semantics for provider ownership
 * - Minimal performance impact (shared_mutex for read-heavy workload)
 * - Compatible with existing consumer APIs (StateCache, CallRouter, HttpServer)
 *
 * Usage Pattern:
 * ```cpp
 * // Read path (concurrent-safe)
 * auto provider = registry.get_provider("provider_id");
 * if (provider && provider->is_available()) {
 *     // use provider
 * }
 *
 * // Write path (exclusive)
 * registry.add_provider("provider_id", std::move(provider_handle));
 * ```
 */
class ProviderRegistry {
public:
    ProviderRegistry() = default;
    ~ProviderRegistry() = default;

    // Non-copyable, non-movable (manages mutex)
    ProviderRegistry(const ProviderRegistry&) = delete;
    ProviderRegistry& operator=(const ProviderRegistry&) = delete;
    ProviderRegistry(ProviderRegistry&&) = delete;
    ProviderRegistry& operator=(ProviderRegistry&&) = delete;

    /**
     * @brief Add or replace a provider
     *
     * If a provider with the same ID already exists, it is replaced.
     * This operation requires exclusive access and will block readers.
     *
     * @param provider_id Unique provider identifier
     * @param provider Shared pointer to provider handle (must not be null)
     */
    void add_provider(const std::string& provider_id, std::shared_ptr<IProviderHandle> provider);

    /**
     * @brief Remove a provider
     *
     * If the provider does not exist, this is a no-op.
     * This operation requires exclusive access and will block readers.
     *
     * @param provider_id Unique provider identifier
     * @return true if provider was removed, false if not found
     */
    bool remove_provider(const std::string& provider_id);

    /**
     * @brief Get a provider by ID
     *
     * Returns a shared_ptr to the provider if found, nullptr otherwise.
     * This operation allows concurrent access with other readers.
     *
     * Thread Safety:
     * - The returned shared_ptr keeps the provider alive even if removed later
     * - Multiple threads can call this simultaneously
     * - Safe to call during provider restart (may return old or new instance)
     *
     * @param provider_id Unique provider identifier
     * @return Shared pointer to provider (nullptr if not found)
     */
    std::shared_ptr<IProviderHandle> get_provider(const std::string& provider_id) const;

    /**
     * @brief Get all providers as a snapshot
     *
     * Returns a copy of the provider map at the time of the call.
     * This is safe for iteration even if the registry is modified concurrently.
     *
     * Thread Safety:
     * - Returns by value to avoid iterator invalidation
     * - Safe to iterate while registry is modified
     * - Shared_ptr keeps providers alive during iteration
     *
     * Performance:
     * - O(n) copy, but necessary for thread safety
     * - Avoid calling in tight loops (cache result when possible)
     *
     * @return Copy of provider map (provider_id -> IProviderHandle)
     */
    std::unordered_map<std::string, std::shared_ptr<IProviderHandle>> get_all_providers() const;

    /**
     * @brief Get list of provider IDs
     *
     * Returns a snapshot of all provider IDs at the time of the call.
     * This is more efficient than get_all_providers() when only IDs are needed.
     *
     * @return Vector of provider IDs
     */
    std::vector<std::string> get_provider_ids() const;

    /**
     * @brief Check if a provider exists
     *
     * Thread-safe existence check without retrieving the provider.
     *
     * @param provider_id Unique provider identifier
     * @return true if provider exists
     */
    bool has_provider(const std::string& provider_id) const;

    /**
     * @brief Get number of registered providers
     *
     * @return Count of providers
     */
    size_t provider_count() const;

    /**
     * @brief Remove all providers
     *
     * This operation requires exclusive access and will block readers.
     * Used during shutdown.
     */
    void clear();

private:
    // Provider storage with thread-safe access
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<IProviderHandle>> providers_;
};

}  // namespace provider
}  // namespace anolis
