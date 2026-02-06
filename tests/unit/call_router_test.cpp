#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "control/call_router.hpp"
#include "mocks/mock_provider_handle.hpp"
#include "registry/device_registry.hpp"
#include "state/state_cache.hpp"

#include "automation/mode_manager.hpp"

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

using namespace anolis;
using namespace testing;
using namespace anolis::tests;

class CallRouterTest : public Test
{
protected:
    void SetUp() override
    {
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
    void RegisterMockDevice()
    {
        // Setup mock provider behavior for discovery
        // Note: The registry will call list_devices then describe_device
        EXPECT_CALL(*mock_provider, list_devices(_))
            .WillOnce(Invoke([](std::vector<Device> &devices)
                             {
                Device dev;
                dev.set_device_id("dev1");
                devices.push_back(dev);
                return true; }));

        EXPECT_CALL(*mock_provider, describe_device("dev1", _))
            .WillOnce(Invoke([](const std::string &, DescribeDeviceResponse &response)
                             {
                 auto* device = response.mutable_device();
                 device->set_device_id("dev1");
                 
                 auto* caps = response.mutable_capabilities();
                 auto* fn = caps->add_functions();
                 fn->set_name("reset");
                 fn->set_function_id(1);
                 return true; }));

        registry->discover_provider("sim0", *mock_provider);
    }

    std::unique_ptr<registry::DeviceRegistry> registry;
    std::unique_ptr<state::StateCache> state_cache;
    std::unique_ptr<control::CallRouter> router;
    std::unique_ptr<automation::ModeManager> mode_manager;
    std::shared_ptr<MockProviderHandle> mock_provider;
    std::unordered_map<std::string, std::shared_ptr<provider::IProviderHandle>> providers;
};

TEST_F(CallRouterTest, ExecuteCallSuccess)
{
    RegisterMockDevice();

    control::CallRequest req;
    req.device_handle = "sim0/dev1";
    req.function_name = "reset";

    // Successful call
    EXPECT_CALL(*mock_provider, call("dev1", _, "reset", _, _))
        .WillOnce(Return(true));

    auto result = router->execute_call(req, providers);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.status_code, anolis::deviceprovider::v0::Status_Code_CODE_OK);
}

TEST_F(CallRouterTest, ProviderUnavailable)
{
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

TEST_F(CallRouterTest, InvalidFunction)
{
    RegisterMockDevice(); // Has "reset"

    control::CallRequest req;
    req.device_handle = "sim0/dev1";
    req.function_name = "explode"; // Not registered

    auto result = router->execute_call(req, providers);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.status_code, anolis::deviceprovider::v0::Status_Code_CODE_NOT_FOUND);
}

TEST_F(CallRouterTest, PreconditionFailure)
{
    RegisterMockDevice();

    control::CallRequest req;
    req.device_handle = "sim0/dev1";
    req.function_name = "reset";

    // Setup CallResult response object (reference param) with status embedded inside it
    // Wait, mock_provider call signature: (const string&, uint32_t, const string&, const ValueMap&, CallResponse&)
    // The CallResponse is the 5th arg.

    EXPECT_CALL(*mock_provider, call("dev1", _, "reset", _, _))
        .WillOnce(Invoke([](const std::string &, uint32_t, const std::string &, const ValueMap &, CallResponse &resp)
                         {
                             // Note: CallResponse doesn't have status code in DPV0 if it fails?
                             // Checking protocol.pb.h above, CallResponse has `results` and `operation_id`.
                             // The STATUS is defined in the top-level Response envelope (spec/device-provider/protocol.proto).
                             // However, the provider interface (IProviderHandle) returns bool.
                             // If it returns false, the router checks last_status_code() of the provider handle?
                             // Let's check IProviderHandle again.
                             return false; // Provider indicates failure
                         }));

    // If call returns false, Router should check provider->last_status_code()
    EXPECT_CALL(*mock_provider, last_status_code())
        .WillRepeatedly(Return(anolis::deviceprovider::v0::Status_Code_CODE_FAILED_PRECONDITION));

    // Router also retrieves error message
    EXPECT_CALL(*mock_provider, last_error())
        .WillRepeatedly(ReturnRef(mock_provider->_err));
    mock_provider->_err = "Precondition failed";

    auto result = router->execute_call(req, providers);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.status_code, anolis::deviceprovider::v0::Status_Code_CODE_FAILED_PRECONDITION);
}

TEST_F(CallRouterTest, PolicyBlockInAuto)
{
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

TEST_F(CallRouterTest, PolicyOverrideInAuto)
{
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
    EXPECT_CALL(*mock_provider, call("dev1", _, "reset", _, _))
        .WillOnce(Return(true));

    auto result = router->execute_call(req, providers);
    EXPECT_TRUE(result.success);
}

TEST_F(CallRouterTest, InvalidArgumentPropagation)
{
    RegisterMockDevice();

    control::CallRequest req;
    req.device_handle = "sim0/dev1";
    req.function_name = "reset";

    // Setup provider to return failure
    EXPECT_CALL(*mock_provider, call("dev1", _, "reset", _, _))
        .WillOnce(Return(false));

    // Consistently return the error info 
    // (Note: we use WillRepeatedly to be safe if called multiple times or order varies slightly)
    EXPECT_CALL(*mock_provider, last_status_code())
        .WillRepeatedly(Return(anolis::deviceprovider::v0::Status_Code_CODE_INVALID_ARGUMENT));
    
    mock_provider->_err = "Invalid voltage";
    EXPECT_CALL(*mock_provider, last_error())
        .WillRepeatedly(ReturnRef(mock_provider->_err));

    auto result = router->execute_call(req, providers);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.status_code, anolis::deviceprovider::v0::Status_Code_CODE_INVALID_ARGUMENT);
    EXPECT_THAT(result.error_message, HasSubstr("Invalid voltage"));
}
