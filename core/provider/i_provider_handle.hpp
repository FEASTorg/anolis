#pragma once

#include <map>
#include <string>
#include <vector>

#include "protocol.pb.h"

namespace anolis {
namespace provider {

// Interface for ProviderHandle to enable mocking
class IProviderHandle {
public:
    virtual ~IProviderHandle() = default;

    // Lifecycle
    virtual bool start() = 0;
    virtual bool is_available() const = 0;

    // ADPP Operations
    virtual bool hello(anolis::deviceprovider::v1::HelloResponse &response) = 0;

    virtual bool list_devices(std::vector<anolis::deviceprovider::v1::Device> &devices) = 0;

    virtual bool describe_device(const std::string &device_id,
                                 anolis::deviceprovider::v1::DescribeDeviceResponse &response) = 0;

    virtual bool read_signals(const std::string &device_id, const std::vector<std::string> &signal_ids,
                              anolis::deviceprovider::v1::ReadSignalsResponse &response) = 0;

    virtual bool call(const std::string &device_id, uint32_t function_id, const std::string &function_name,
                      const std::map<std::string, anolis::deviceprovider::v1::Value> &args,
                      anolis::deviceprovider::v1::CallResponse &response) = 0;

    // Status
    virtual const std::string &last_error() const = 0;
    virtual anolis::deviceprovider::v1::Status_Code last_status_code() const = 0;
    virtual const std::string &provider_id() const = 0;
};

}  // namespace provider
}  // namespace anolis
