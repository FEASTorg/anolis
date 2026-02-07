#ifndef ANOLIS_REGISTRY_DEVICE_REGISTRY_HPP
#define ANOLIS_REGISTRY_DEVICE_REGISTRY_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "protocol.pb.h"
#include "provider/i_provider_handle.hpp"  // Changed to interface

namespace anolis {
namespace registry {

// Signal specification for quick lookup
struct SignalSpec {
    std::string signal_id;
    std::string label;
    anolis::deviceprovider::v0::ValueType type;
    bool readable;
    bool writable;
    bool is_default;  // Polled automatically
};

// Function specification for quick lookup
struct FunctionSpec {
    uint32_t function_id;
    std::string function_name;
    std::string label;
    std::vector<anolis::deviceprovider::v0::ArgSpec> args;  // Full ArgSpec for validation
};

// Immutable device capabilities (populated from DescribeDevice)
// Named DeviceCapabilitySet to avoid Windows macro collision with DeviceCapabilities
struct DeviceCapabilitySet {
    // Raw protobuf (for serialization/inspection)
    anolis::deviceprovider::v0::Device proto;

    // Lookup maps (for fast validation)
    std::unordered_map<std::string, SignalSpec> signals_by_id;
    std::unordered_map<std::string, FunctionSpec> functions_by_id;
};

// Registered device (provider + device metadata)
struct RegisteredDevice {
    std::string provider_id;  // Configured name (e.g., "sim0")
    std::string device_id;    // Provider-local ID (e.g., "tempctl0")
    DeviceCapabilitySet capabilities;

    // Composite key for global device handle
    std::string get_handle() const { return provider_id + "/" + device_id; }
};

// Device Registry - Immutable inventory after discovery
class DeviceRegistry {
public:
    DeviceRegistry() = default;

    // Discovery: Perform Hello -> ListDevices -> DescribeDevice for each device
    bool discover_provider(const std::string &provider_id, anolis::provider::IProviderHandle &provider);

    // Lookup
    const RegisteredDevice *get_device(const std::string &provider_id, const std::string &device_id) const;
    const RegisteredDevice *get_device_by_handle(const std::string &handle) const;

    // Iteration
    std::vector<const RegisteredDevice *> get_all_devices() const;
    std::vector<const RegisteredDevice *> get_devices_for_provider(const std::string &provider_id) const;

    // Management
    void clear_provider_devices(const std::string &provider_id);

    // Status
    size_t device_count() const { return devices_.size(); }
    const std::string &last_error() const { return error_; }

private:
    // Storage: vector for stable pointers, map for fast lookup
    std::vector<RegisteredDevice> devices_;
    std::unordered_map<std::string, size_t> handle_to_index_;  // "provider/device" -> index

    std::string error_;

    // Helper: Build capability maps from protobuf
    bool build_capabilities(const anolis::deviceprovider::v0::Device &proto_device,
                            const anolis::deviceprovider::v0::CapabilitySet &proto_caps, DeviceCapabilitySet &caps);
};

}  // namespace registry
}  // namespace anolis

#endif  // ANOLIS_REGISTRY_DEVICE_REGISTRY_HPP
