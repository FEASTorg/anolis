#pragma once
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "provider/i_provider_handle.hpp"
#include "protocol.pb.h"

#include <vector>
#include <string>
#include <map>

namespace anolis::tests {

using namespace anolis;
using namespace testing;
using ProtoValue = anolis::deviceprovider::v0::Value;
using Device = anolis::deviceprovider::v0::Device;
using DescribeDeviceResponse = anolis::deviceprovider::v0::DescribeDeviceResponse;
using ReadSignalsResponse = anolis::deviceprovider::v0::ReadSignalsResponse;
using CallResponse = anolis::deviceprovider::v0::CallResponse;
using ValueMap = std::map<std::string, ProtoValue>;

class MockProviderHandle : public provider::IProviderHandle {
public:
    MOCK_METHOD(bool, start, (), (override));
    MOCK_METHOD(bool, is_available, (), (const, override));
    
    MOCK_METHOD(bool, hello, (anolis::deviceprovider::v0::HelloResponse &), (override));
    MOCK_METHOD(bool, list_devices, (std::vector<Device> &), (override));
    MOCK_METHOD(bool, describe_device, (const std::string &, DescribeDeviceResponse &), (override));
    MOCK_METHOD(bool, read_signals, (const std::string &, const std::vector<std::string> &, ReadSignalsResponse &), (override));
    MOCK_METHOD(bool, call, (const std::string &, uint32_t, const std::string &, const ValueMap &, CallResponse &), (override));


    MOCK_METHOD(const std::string &, last_error, (), (const, override));
    MOCK_METHOD(anolis::deviceprovider::v0::Status_Code, last_status_code, (), (const, override));
    MOCK_METHOD(const std::string &, provider_id, (), (const, override));

    // Helper to store/return ID reference
    std::string _id = "sim0";
    std::string _err = "";
};

} // namespace anolis::tests
