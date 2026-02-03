#include "device_registry.hpp"
#include <iostream>

namespace anolis {
namespace registry {

bool DeviceRegistry::discover_provider(const std::string& provider_id,
                                       anolis::provider::ProviderHandle& provider) {
    std::cerr << "[Registry] Discovering provider: " << provider_id << "\n";
    
    // Step 1: ListDevices
    std::vector<anolis::deviceprovider::v0::Device> device_list;
    if (!provider.list_devices(device_list)) {
        error_ = "ListDevices failed: " + provider.last_error();
        std::cerr << "[Registry] ERROR: " << error_ << "\n";
        return false;
    }
    
    std::cerr << "[Registry] Found " << device_list.size() << " devices\n";
    
    // Step 2: DescribeDevice for each
    for (const auto& device_brief : device_list) {
        const std::string& device_id = device_brief.device_id();
        std::cerr << "[Registry] Describing device: " << device_id << "\n";
        
        anolis::deviceprovider::v0::DescribeDeviceResponse describe_response;
        if (!provider.describe_device(device_id, describe_response)) {
            error_ = "DescribeDevice(" + device_id + ") failed: " + provider.last_error();
            std::cerr << "[Registry] ERROR: " << error_ << "\n";
            return false;
        }
        
        // Build capabilities
        RegisteredDevice reg_device;
        reg_device.provider_id = provider_id;
        reg_device.device_id = device_id;
        
        if (!build_capabilities(describe_response.device(), 
                               describe_response.capabilities(),
                               reg_device.capabilities)) {
            std::cerr << "[Registry] ERROR: " << error_ << "\n";
            return false;
        }
        
        // Store device
        std::string handle = reg_device.get_handle();
        handle_to_index_[handle] = devices_.size();
        devices_.push_back(std::move(reg_device));
        
        std::cerr << "[Registry] Registered: " << handle 
                  << " (" << reg_device.capabilities.signals_by_id.size() << " signals, "
                  << reg_device.capabilities.functions_by_id.size() << " functions)\n";
    }
    
    return true;
}

const RegisteredDevice* DeviceRegistry::get_device(const std::string& provider_id,
                                                   const std::string& device_id) const {
    std::string handle = provider_id + "/" + device_id;
    return get_device_by_handle(handle);
}

const RegisteredDevice* DeviceRegistry::get_device_by_handle(const std::string& handle) const {
    auto it = handle_to_index_.find(handle);
    if (it == handle_to_index_.end()) {
        return nullptr;
    }
    return &devices_[it->second];
}

std::vector<const RegisteredDevice*> DeviceRegistry::get_all_devices() const {
    std::vector<const RegisteredDevice*> result;
    result.reserve(devices_.size());
    for (const auto& device : devices_) {
        result.push_back(&device);
    }
    return result;
}

std::vector<const RegisteredDevice*> DeviceRegistry::get_devices_for_provider(
    const std::string& provider_id) const {
    std::vector<const RegisteredDevice*> result;
    for (const auto& device : devices_) {
        if (device.provider_id == provider_id) {
            result.push_back(&device);
        }
    }
    return result;
}

bool DeviceRegistry::build_capabilities(const anolis::deviceprovider::v0::Device& proto_device,
                                        const anolis::deviceprovider::v0::CapabilitySet& proto_caps,
                                        DeviceCapabilitySet& caps) {
    // Store raw proto
    caps.proto = proto_device;
    
    // Build signal lookup map
    for (const auto& signal : proto_caps.signals()) {
        SignalSpec spec;
        spec.signal_id = signal.signal_id();
        spec.label = signal.name();
        spec.type = signal.value_type();
        spec.readable = true;  // All signals readable in v0
        spec.writable = false; // No writable signals in v0
        spec.is_default = (signal.poll_hint_hz() > 0);  // Default if poll hint set
        
        caps.signals_by_id[spec.signal_id] = spec;
    }
    
    // Build function lookup map
    for (const auto& function : proto_caps.functions()) {
        FunctionSpec spec;
        spec.function_id = function.function_id();
        spec.function_name = function.name();
        spec.label = function.description();
        
        for (const auto& arg : function.args()) {
            spec.param_ids.push_back(arg.name());
        }
        
        caps.functions_by_id[spec.function_name] = spec;
    }
    
    return true;
}

} // namespace registry
} // namespace anolis
