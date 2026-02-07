#include "control/call_router.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "automation/mode_manager.hpp"
#include "mocks/mock_provider_handle.hpp"
#include "registry/device_registry.hpp"
#include "state/state_cache.hpp"

using namespace anolis;
using namespace testing;
using namespace anolis::tests;

class CallRouterTest : public Test {
protected:
    void SetUp() override {
        registry = std::make_unique<registry::DeviceRegistry>();
        state_cache = std::make_unique<state::StateCache>(*registry, 100);
        router = std::make_unique<control::CallRouter>(*registry, *state_cache);
        mode_manager = std::make_unique<automation::ModeManager>();

        // Default: Connect router to mode manager with NO blocking
        router->set_mode_manager(mode_manager.get(), "OVERRIDE");

        mock_provider = std::make_shared<StrictMock<MockProviderHandle>>();
        mock_provider->_id = "sim0";
        EXPECT_CALL(*mock_provider, provider_id()).WillRepeatedly(ReturnRef(mock_provider->_id));
        EXPECT_CALL(*mock_provider, is_available()).WillRepeatedly(Return(true));

        providers["sim0"] = mock_provider;
    }

    // ... existing RegisterMockDevice ...
    void RegisterMockDevice() {
        // Setup mock provider behavior for discovery
        // Note: The registry will call list_devices then describe_device
        EXPECT_CALL(*mock_provider, list_devices(_)).WillOnce(Invoke([](std::vector<Device>& devices) {
            Device dev;
            dev.set_device_id("dev1");
            devices.push_back(dev);
            return true;
        }));

        EXPECT_CALL(*mock_provider, describe_device("dev1", _))
            .WillOnce(Invoke([](const std::string&, DescribeDeviceResponse& response) {
                auto* device = response.mutable_device();
                device->set_device_id("dev1");

                auto* caps = response.mutable_capabilities();
                auto* fn = caps->add_functions();
                fn->set_name("reset");
                fn->set_function_id(1);
                return true;
            }));

        registry->discover_provider("sim0", *mock_provider);
    }

    std::unique_ptr<registry::DeviceRegistry> registry;
    std::unique_ptr<state::StateCache> state_cache;
    std::unique_ptr<control::CallRouter> router;
    std::unique_ptr<automation::ModeManager> mode_manager;
    std::shared_ptr<MockProviderHandle> mock_provider;
    std::unordered_map<std::string, std::shared_ptr<provider::IProviderHandle>> providers;
};

TEST_F(CallRouterTest, ExecuteCallSuccess) {
    RegisterMockDevice();

    control::CallRequest req;
    req.device_handle = "sim0/dev1";
    req.function_name = "reset";

    // Successful call
    EXPECT_CALL(*mock_provider, call("dev1", _, "reset", _, _)).WillOnce(Return(true));

    auto result = router->execute_call(req, providers);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.status_code, anolis::deviceprovider::v0::Status_Code_CODE_OK);
}

TEST_F(CallRouterTest, ProviderUnavailable) {
    RegisterMockDevice();

    // Override is_available to false for the call check
    // Note: poll_once or other valid calls usually check this.
    // CallRouter checks it explicitly.
    EXPECT_CALL(*mock_provider, is_available()).WillRepeatedly(Return(false));

    control::CallRequest req;
    req.device_handle = "sim0/dev1";
    req.function_name = "reset";

    auto result = router->execute_call(req, providers);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.status_code, anolis::deviceprovider::v0::Status_Code_CODE_UNAVAILABLE);
}

TEST_F(CallRouterTest, InvalidFunction) {
    RegisterMockDevice();  // Has "reset"

    control::CallRequest req;
    req.device_handle = "sim0/dev1";
    req.function_name = "explode";  // Not registered

    auto result = router->execute_call(req, providers);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.status_code, anolis::deviceprovider::v0::Status_Code_CODE_NOT_FOUND);
}

TEST_F(CallRouterTest, PreconditionFailure) {
    RegisterMockDevice();

    control::CallRequest req;
    req.device_handle = "sim0/dev1";
    req.function_name = "reset";

    // Setup CallResult response object (reference param) with status embedded inside it
    // Wait, mock_provider call signature: (const string&, uint32_t, const string&, const ValueMap&, CallResponse&)
    // The CallResponse is the 5th arg.

    EXPECT_CALL(*mock_provider, call("dev1", _, "reset", _, _))
        .WillOnce(Invoke([](const std::string&, uint32_t, const std::string&, const ValueMap&, CallResponse& resp) {
            // Note: CallResponse doesn't have status code in DPV0 if it fails?
            // Checking protocol.pb.h above, CallResponse has `results` and `operation_id`.
            // The STATUS is defined in the top-level Response envelope (spec/device-provider/protocol.proto).
            // However, the provider interface (IProviderHandle) returns bool.
            // If it returns false, the router checks last_status_code() of the provider handle?
            // Let's check IProviderHandle again.
            return false;  // Provider indicates failure
        }));

    // If call returns false, Router should check provider->last_status_code()
    EXPECT_CALL(*mock_provider, last_status_code())
        .WillRepeatedly(Return(anolis::deviceprovider::v0::Status_Code_CODE_FAILED_PRECONDITION));

    // Router also retrieves error message
    EXPECT_CALL(*mock_provider, last_error()).WillRepeatedly(ReturnRef(mock_provider->_err));
    mock_provider->_err = "Precondition failed";

    auto result = router->execute_call(req, providers);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.status_code, anolis::deviceprovider::v0::Status_Code_CODE_FAILED_PRECONDITION);
}

TEST_F(CallRouterTest, PolicyBlockInAuto) {
    RegisterMockDevice();

    // 1. Set policy to BLOCK
    router->set_mode_manager(mode_manager.get(), "BLOCK");

    std::string err;
    ASSERT_TRUE(mode_manager->set_mode(automation::RuntimeMode::AUTO, err));

    control::CallRequest req;
    req.device_handle = "sim0/dev1";
    req.function_name = "reset";

    // Expect NO CALL to provider
    EXPECT_CALL(*mock_provider, call(_, _, _, _, _)).Times(0);

    auto result = router->execute_call(req, providers);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.status_code, anolis::deviceprovider::v0::Status_Code_CODE_FAILED_PRECONDITION);
    EXPECT_THAT(result.error_message, HasSubstr("blocked in AUTO"));
}

TEST_F(CallRouterTest, PolicyOverrideInAuto) {
    RegisterMockDevice();

    // 1. Set policy to OVERRIDE (Explicitly allow)
    router->set_mode_manager(mode_manager.get(), "OVERRIDE");

    // 2. Set mode to AUTO
    std::string err;
    ASSERT_TRUE(mode_manager->set_mode(automation::RuntimeMode::AUTO, err));

    control::CallRequest req;
    req.device_handle = "sim0/dev1";
    req.function_name = "reset";

    // Expect CALL to provider
    EXPECT_CALL(*mock_provider, call("dev1", _, "reset", _, _)).WillOnce(Return(true));

    auto result = router->execute_call(req, providers);
    EXPECT_TRUE(result.success);
}

TEST_F(CallRouterTest, InvalidArgumentPropagation) {
    RegisterMockDevice();

    control::CallRequest req;
    req.device_handle = "sim0/dev1";
    req.function_name = "reset";

    // Setup provider to return failure
    EXPECT_CALL(*mock_provider, call("dev1", _, "reset", _, _)).WillOnce(Return(false));

    // Consistently return the error info
    // (Note: we use WillRepeatedly to be safe if called multiple times or order varies slightly)
    EXPECT_CALL(*mock_provider, last_status_code())
        .WillRepeatedly(Return(anolis::deviceprovider::v0::Status_Code_CODE_INVALID_ARGUMENT));

    mock_provider->_err = "Invalid voltage";
    EXPECT_CALL(*mock_provider, last_error()).WillRepeatedly(ReturnRef(mock_provider->_err));

    auto result = router->execute_call(req, providers);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.status_code, anolis::deviceprovider::v0::Status_Code_CODE_INVALID_ARGUMENT);
    EXPECT_THAT(result.error_message, HasSubstr("Invalid voltage"));
}

// ============================================================================
// Validation Tests - Test argument validation
// ============================================================================

class CallRouterValidationTest : public Test {
protected:
    using ValueType = anolis::deviceprovider::v0::ValueType;
    using ArgSpec = anolis::deviceprovider::v0::ArgSpec;
    using Value = anolis::deviceprovider::v0::Value;

    void SetUp() override {
        registry = std::make_unique<registry::DeviceRegistry>();
        state_cache = std::make_unique<state::StateCache>(*registry, 100);
        router = std::make_unique<control::CallRouter>(*registry, *state_cache);

        mock_provider = std::make_shared<StrictMock<MockProviderHandle>>();
        mock_provider->_id = "sim0";
        EXPECT_CALL(*mock_provider, provider_id()).WillRepeatedly(ReturnRef(mock_provider->_id));
        EXPECT_CALL(*mock_provider, is_available()).WillRepeatedly(Return(true));

        providers["sim0"] = mock_provider;

        // Register device with function specs for validation testing
        RegisterValidationDevice();
    }

    void RegisterValidationDevice() {
        EXPECT_CALL(*mock_provider, list_devices(_)).WillOnce(Invoke([](std::vector<Device>& devices) {
            Device dev;
            dev.set_device_id("testdev");
            devices.push_back(dev);
            return true;
        }));

        EXPECT_CALL(*mock_provider, describe_device("testdev", _))
            .WillOnce(Invoke([](const std::string&, DescribeDeviceResponse& response) {
                auto* device = response.mutable_device();
                device->set_device_id("testdev");

                auto* caps = response.mutable_capabilities();

                // Function: set_temperature(value: double[0.0-100.0] required, unit: string optional)
                auto* fn1 = caps->add_functions();
                fn1->set_name("set_temperature");
                fn1->set_function_id(1);

                auto* arg1 = fn1->add_args();
                arg1->set_name("value");
                arg1->set_type(ValueType::VALUE_TYPE_DOUBLE);
                arg1->set_required(true);
                arg1->set_min_double(0.0);
                arg1->set_max_double(100.0);
                arg1->set_description("Temperature value");
                arg1->set_unit("C");

                auto* arg2 = fn1->add_args();
                arg2->set_name("unit");
                arg2->set_type(ValueType::VALUE_TYPE_STRING);
                arg2->set_required(false);
                arg2->set_description("Temperature unit");

                // Function: set_index(index: int64[1-10] required)
                auto* fn2 = caps->add_functions();
                fn2->set_name("set_index");
                fn2->set_function_id(2);

                auto* arg3 = fn2->add_args();
                arg3->set_name("index");
                arg3->set_type(ValueType::VALUE_TYPE_INT64);
                arg3->set_required(true);
                arg3->set_min_int64(1);
                arg3->set_max_int64(10);

                // Function: set_flag(enabled: bool required)
                auto* fn3 = caps->add_functions();
                fn3->set_name("set_flag");
                fn3->set_function_id(3);

                auto* arg4 = fn3->add_args();
                arg4->set_name("enabled");
                arg4->set_type(ValueType::VALUE_TYPE_BOOL);
                arg4->set_required(true);

                return true;
            }));

        registry->discover_provider("sim0", *mock_provider);
    }

    Value make_double(double val) {
        Value v;
        v.set_type(ValueType::VALUE_TYPE_DOUBLE);
        v.set_double_value(val);
        return v;
    }

    Value make_int64(int64_t val) {
        Value v;
        v.set_type(ValueType::VALUE_TYPE_INT64);
        v.set_int64_value(val);
        return v;
    }

    Value make_string(const std::string& val) {
        Value v;
        v.set_type(ValueType::VALUE_TYPE_STRING);
        v.set_string_value(val);
        return v;
    }

    Value make_bool(bool val) {
        Value v;
        v.set_type(ValueType::VALUE_TYPE_BOOL);
        v.set_bool_value(val);
        return v;
    }

    std::unique_ptr<registry::DeviceRegistry> registry;
    std::unique_ptr<state::StateCache> state_cache;
    std::unique_ptr<control::CallRouter> router;
    std::shared_ptr<MockProviderHandle> mock_provider;
    std::unordered_map<std::string, std::shared_ptr<provider::IProviderHandle>> providers;
};

TEST_F(CallRouterValidationTest, ValidCallWithRequiredArgs) {
    control::CallRequest req;
    req.device_handle = "sim0/testdev";
    req.function_name = "set_temperature";
    req.args["value"] = make_double(50.0);

    std::string error;
    EXPECT_TRUE(router->validate_call(req, error)) << "Error: " << error;
}

TEST_F(CallRouterValidationTest, ValidCallWithOptionalArgs) {
    control::CallRequest req;
    req.device_handle = "sim0/testdev";
    req.function_name = "set_temperature";
    req.args["value"] = make_double(50.0);
    req.args["unit"] = make_string("C");

    std::string error;
    EXPECT_TRUE(router->validate_call(req, error)) << "Error: " << error;
}

TEST_F(CallRouterValidationTest, MissingRequiredArgument) {
    control::CallRequest req;
    req.device_handle = "sim0/testdev";
    req.function_name = "set_temperature";
    // Missing required "value" argument

    std::string error;
    EXPECT_FALSE(router->validate_call(req, error));
    EXPECT_THAT(error, HasSubstr("Missing required argument: value"));
}

TEST_F(CallRouterValidationTest, TypeMismatchDouble) {
    control::CallRequest req;
    req.device_handle = "sim0/testdev";
    req.function_name = "set_temperature";
    req.args["value"] = make_string("not a number");  // Wrong type

    std::string error;
    EXPECT_FALSE(router->validate_call(req, error));
    EXPECT_THAT(error, HasSubstr("Type mismatch"));
    EXPECT_THAT(error, HasSubstr("double"));
}

TEST_F(CallRouterValidationTest, TypeMismatchInt64) {
    control::CallRequest req;
    req.device_handle = "sim0/testdev";
    req.function_name = "set_index";
    req.args["index"] = make_double(5.0);  // Wrong type (double instead of int64)

    std::string error;
    EXPECT_FALSE(router->validate_call(req, error));
    EXPECT_THAT(error, HasSubstr("Type mismatch"));
    EXPECT_THAT(error, HasSubstr("int64"));
}

TEST_F(CallRouterValidationTest, TypeMismatchBool) {
    control::CallRequest req;
    req.device_handle = "sim0/testdev";
    req.function_name = "set_flag";
    req.args["enabled"] = make_int64(1);  // Wrong type (int instead of bool)

    std::string error;
    EXPECT_FALSE(router->validate_call(req, error));
    EXPECT_THAT(error, HasSubstr("Type mismatch"));
    EXPECT_THAT(error, HasSubstr("bool"));
}

TEST_F(CallRouterValidationTest, DoubleOutOfRangeTooLow) {
    control::CallRequest req;
    req.device_handle = "sim0/testdev";
    req.function_name = "set_temperature";
    req.args["value"] = make_double(-10.0);  // Below min (0.0)

    std::string error;
    EXPECT_FALSE(router->validate_call(req, error));
    EXPECT_THAT(error, HasSubstr("below minimum"));
}

TEST_F(CallRouterValidationTest, DoubleOutOfRangeTooHigh) {
    control::CallRequest req;
    req.device_handle = "sim0/testdev";
    req.function_name = "set_temperature";
    req.args["value"] = make_double(150.0);  // Above max (100.0)

    std::string error;
    EXPECT_FALSE(router->validate_call(req, error));
    EXPECT_THAT(error, HasSubstr("above maximum"));
}

TEST_F(CallRouterValidationTest, DoubleAtBoundaries) {
    control::CallRequest req;
    req.device_handle = "sim0/testdev";
    req.function_name = "set_temperature";

    // Test min boundary
    req.args["value"] = make_double(0.0);
    std::string error;
    EXPECT_TRUE(router->validate_call(req, error)) << "Error: " << error;

    // Test max boundary
    req.args["value"] = make_double(100.0);
    EXPECT_TRUE(router->validate_call(req, error)) << "Error: " << error;
}

TEST_F(CallRouterValidationTest, Int64OutOfRangeTooLow) {
    control::CallRequest req;
    req.device_handle = "sim0/testdev";
    req.function_name = "set_index";
    req.args["index"] = make_int64(0);  // Below min (1)

    std::string error;
    EXPECT_FALSE(router->validate_call(req, error));
    EXPECT_THAT(error, HasSubstr("below minimum"));
}

TEST_F(CallRouterValidationTest, Int64OutOfRangeTooHigh) {
    control::CallRequest req;
    req.device_handle = "sim0/testdev";
    req.function_name = "set_index";
    req.args["index"] = make_int64(20);  // Above max (10)

    std::string error;
    EXPECT_FALSE(router->validate_call(req, error));
    EXPECT_THAT(error, HasSubstr("above maximum"));
}

TEST_F(CallRouterValidationTest, Int64AtBoundaries) {
    control::CallRequest req;
    req.device_handle = "sim0/testdev";
    req.function_name = "set_index";

    // Test min boundary
    req.args["index"] = make_int64(1);
    std::string error;
    EXPECT_TRUE(router->validate_call(req, error)) << "Error: " << error;

    // Test max boundary
    req.args["index"] = make_int64(10);
    EXPECT_TRUE(router->validate_call(req, error)) << "Error: " << error;
}

TEST_F(CallRouterValidationTest, UnknownArgument) {
    control::CallRequest req;
    req.device_handle = "sim0/testdev";
    req.function_name = "set_temperature";
    req.args["value"] = make_double(50.0);
    req.args["unknown_param"] = make_string("oops");  // Not in function spec

    std::string error;
    EXPECT_FALSE(router->validate_call(req, error));
    EXPECT_THAT(error, HasSubstr("Unknown argument: unknown_param"));
}

TEST_F(CallRouterValidationTest, OptionalArgumentCanBeOmitted) {
    control::CallRequest req;
    req.device_handle = "sim0/testdev";
    req.function_name = "set_temperature";
    req.args["value"] = make_double(50.0);
    // Omit optional "unit" argument

    std::string error;
    EXPECT_TRUE(router->validate_call(req, error)) << "Error: " << error;
}
