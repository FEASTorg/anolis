#include "provider_registry.hpp"

#include <algorithm>
#include <mutex>

namespace anolis {
namespace provider {

void ProviderRegistry::add_provider(const std::string& provider_id, std::shared_ptr<IProviderHandle> provider) {
    // Require exclusive access for modification
    std::unique_lock<std::shared_mutex> lock(mutex_);
    providers_[provider_id] = std::move(provider);
}

bool ProviderRegistry::remove_provider(const std::string& provider_id) {
    // Require exclusive access for modification
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return providers_.erase(provider_id) > 0;
}

std::shared_ptr<IProviderHandle> ProviderRegistry::get_provider(const std::string& provider_id) const {
    // Shared read access - multiple threads can read concurrently
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = providers_.find(provider_id);
    if (it != providers_.end()) {
        return it->second;  // Return shared_ptr copy (increments ref count)
    }
    return nullptr;
}

std::unordered_map<std::string, std::shared_ptr<IProviderHandle>> ProviderRegistry::get_all_providers() const {
    // Shared read access - return by value to avoid iterator invalidation
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return providers_;  // Copy the map (shared_ptr copies are cheap - ref count increment)
}

std::vector<std::string> ProviderRegistry::get_provider_ids() const {
    // Shared read access
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::vector<std::string> ids;
    ids.reserve(providers_.size());

    for (const auto& [id, provider] : providers_) {
        ids.push_back(id);
    }

    return ids;
}

bool ProviderRegistry::has_provider(const std::string& provider_id) const {
    // Shared read access
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return providers_.find(provider_id) != providers_.end();
}

size_t ProviderRegistry::provider_count() const {
    // Shared read access
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return providers_.size();
}

void ProviderRegistry::clear() {
    // Require exclusive access for modification
    std::unique_lock<std::shared_mutex> lock(mutex_);
    providers_.clear();
}

}  // namespace provider
}  // namespace anolis
