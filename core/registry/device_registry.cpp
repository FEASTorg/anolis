#include "device_registry.hpp"

#include <iostream>

#include "logging/logger.hpp"

namespace anolis {
namespace registry {

bool DeviceRegistry::discover_provider(const std::string &provider_id, anolis::provider::IProviderHandle &provider) {
    LOG_INFO("[Registry] Discovering provider: " << provider_id);

    // Step 1: ListDevices
    std::vector<anolis::deviceprovider::v0::Device> device_list;
    if (!provider.list_devices(device_list)) {
        error_ = "ListDevices failed: " + provider.last_error();
        LOG_ERROR("[Registry] " << error_);
        return false;
    }

    LOG_INFO("[Registry] Found " << device_list.size() << " devices");

    // Build devices outside lock (discovery is network I/O)
    std::vector<RegisteredDevice> new_devices;
    new_devices.reserve(device_list.size());

    // Step 2: DescribeDevice for each
    for (const auto &device_brief : device_list) {
        const std::string &device_id = device_brief.device_id();
        LOG_INFO("[Registry] Describing device: " << device_id);

        anolis::deviceprovider::v0::DescribeDeviceResponse describe_response;
        if (!provider.describe_device(device_id, describe_response)) {
            error_ = "DescribeDevice(" + device_id + ") failed: " + provider.last_error();
            LOG_ERROR("[Registry] " << error_);
            return false;
        }

        // Build capabilities
        RegisteredDevice reg_device;
        reg_device.provider_id = provider_id;
        reg_device.device_id = device_id;

        if (!build_capabilities(describe_response.device(), describe_response.capabilities(),
                                reg_device.capabilities)) {
            LOG_ERROR("[Registry] " << error_);
            return false;
        }

        // Log BEFORE the move
        std::string handle = reg_device.get_handle();
        LOG_INFO("[Registry] Registered: " << handle << " (" << reg_device.capabilities.signals_by_id.size()
                                           << " signals, " << reg_device.capabilities.functions_by_id.size()
                                           << " functions)");

        new_devices.push_back(std::move(reg_device));
    }

    // Step 3: Insert into registry under lock (fast operation)
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        for (auto &device : new_devices) {
            std::string handle = device.get_handle();
            handle_to_index_[handle] = devices_.size();
            devices_.push_back(std::move(device));
        }
    }

    return true;
}

std::optional<RegisteredDevice> DeviceRegistry::get_device_copy(const std::string &provider_id,
                                                                const std::string &device_id) const {
    std::string handle = provider_id + "/" + device_id;
    return get_device_by_handle_copy(handle);
}

std::optional<RegisteredDevice> DeviceRegistry::get_device_by_handle_copy(const std::string &handle) const {
    // Shared read access - multiple threads can read concurrently
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto it = handle_to_index_.find(handle);
    if (it == handle_to_index_.end()) {
        return std::nullopt;
    }
    // Return copy under lock (safe even if registry is cleared by another thread)
    return devices_[it->second];
}

std::vector<RegisteredDevice> DeviceRegistry::get_all_devices() const {
    // Shared read access - return vector copy
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return devices_;  // Copy the vector (RegisteredDevice has implicit copy)
}

std::vector<RegisteredDevice> DeviceRegistry::get_devices_for_provider(const std::string &provider_id) const {
    // Shared read access
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::vector<RegisteredDevice> result;
    for (const auto &device : devices_) {
        if (device.provider_id == provider_id) {
            result.push_back(device);  // Copy device
        }
    }
    return result;
}

void DeviceRegistry::clear_provider_devices(const std::string &provider_id) {
    LOG_INFO("[Registry] Clearing devices for provider: " << provider_id);

    // Exclusive write access
    std::unique_lock<std::shared_mutex> lock(mutex_);

    // Remove all devices matching provider_id
    auto new_end = std::remove_if(devices_.begin(), devices_.end(), [&provider_id](const RegisteredDevice &device) {
        return device.provider_id == provider_id;
    });

    size_t removed_count = std::distance(new_end, devices_.end());
    devices_.erase(new_end, devices_.end());

    // Rebuild handle index since indices may have shifted
    handle_to_index_.clear();
    for (size_t i = 0; i < devices_.size(); ++i) {
        handle_to_index_[devices_[i].get_handle()] = i;
    }

    LOG_INFO("[Registry] Removed " << removed_count << " devices from provider " << provider_id);
}

size_t DeviceRegistry::device_count() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return devices_.size();
}

std::string DeviceRegistry::last_error() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return error_;
}

bool DeviceRegistry::build_capabilities(const anolis::deviceprovider::v0::Device &proto_device,
                                        const anolis::deviceprovider::v0::CapabilitySet &proto_caps,
                                        DeviceCapabilitySet &caps) {
    // Store raw proto
    caps.proto = proto_device;

    // Build signal lookup map
    for (const auto &signal : proto_caps.signals()) {
        SignalSpec spec;
        spec.signal_id = signal.signal_id();
        spec.label = signal.name();
        spec.type = signal.value_type();
        spec.readable = true;                           // All signals readable in v0
        spec.writable = false;                          // No writable signals in v0
        spec.is_default = (signal.poll_hint_hz() > 0);  // Default if poll hint set

        caps.signals_by_id[spec.signal_id] = spec;
    }

    // Build function lookup map
    for (const auto &function : proto_caps.functions()) {
        FunctionSpec spec;
        spec.function_id = function.function_id();
        spec.function_name = function.name();
        spec.label = function.description();

        // Store full ArgSpec for validation (type, required, min/max constraints)
        for (const auto &arg : function.args()) {
            spec.args.push_back(arg);
        }

        caps.functions_by_id[spec.function_name] = spec;
    }

    return true;
}

}  // namespace registry
}  // namespace anolis
