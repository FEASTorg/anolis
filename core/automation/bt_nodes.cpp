#include "automation/bt_nodes.hpp"

#include <iostream>
#include <sstream>

#include "automation/parameter_manager.hpp"
#include "control/call_router.hpp"
#include "logging/logger.hpp"
#include "provider/i_provider_handle.hpp"  // Added include
#include "state/state_cache.hpp"

namespace anolis {
namespace automation {

// Helper: Convert protobuf Value to double (for output port)
static double value_to_double(const anolis::deviceprovider::v0::Value &val) {
    switch (val.kind_case()) {
        case anolis::deviceprovider::v0::Value::kDoubleValue:
            return val.double_value();
        case anolis::deviceprovider::v0::Value::kInt64Value:
            return static_cast<double>(val.int64_value());
        case anolis::deviceprovider::v0::Value::kUint64Value:
            return static_cast<double>(val.uint64_value());
        case anolis::deviceprovider::v0::Value::kBoolValue:
            return val.bool_value() ? 1.0 : 0.0;
        default:
            return 0.0;  // Default for string/bytes/unspecified
    }
}

// Helper: Convert protobuf Value to string (for output port)
static std::string value_to_string(const anolis::deviceprovider::v0::Value &val) {
    switch (val.kind_case()) {
        case anolis::deviceprovider::v0::Value::kDoubleValue:
            return std::to_string(val.double_value());
        case anolis::deviceprovider::v0::Value::kInt64Value:
            return std::to_string(val.int64_value());
        case anolis::deviceprovider::v0::Value::kUint64Value:
            return std::to_string(val.uint64_value());
        case anolis::deviceprovider::v0::Value::kBoolValue:
            return val.bool_value() ? "true" : "false";
        case anolis::deviceprovider::v0::Value::kStringValue:
            return val.string_value();
        case anolis::deviceprovider::v0::Value::kBytesValue:
            return "<bytes>";
        default:
            return "<empty>";
    }
}

// Helper: Convert quality enum to string
static std::string quality_to_string(anolis::deviceprovider::v0::SignalValue_Quality q) {
    switch (q) {
        case anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_OK:
            return "OK";
        case anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_STALE:
            return "STALE";
        case anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_FAULT:
            return "FAULT";
        case anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_UNKNOWN:
            return "UNKNOWN";
        default:
            return "UNSPECIFIED";
    }
}

// Helper: Convert string to quality enum
static anolis::deviceprovider::v0::SignalValue_Quality string_to_quality(const std::string &s) {
    if (s == "OK") return anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_OK;
    if (s == "STALE") return anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_STALE;
    if (s == "FAULT") return anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_FAULT;
    if (s == "UNKNOWN") return anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_UNKNOWN;
    return anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_UNSPECIFIED;
}

// Helper: Create protobuf Value from double
static anolis::deviceprovider::v0::Value double_to_value(double d) {
    anolis::deviceprovider::v0::Value val;
    val.set_type(anolis::deviceprovider::v0::VALUE_TYPE_DOUBLE);
    val.set_double_value(d);
    return val;
}

//-----------------------------------------------------------------------------
// ReadSignalNode
//-----------------------------------------------------------------------------

ReadSignalNode::ReadSignalNode(const std::string &name, const BT::NodeConfig &config)
    : BT::SyncActionNode(name, config) {}

BT::PortsList ReadSignalNode::providedPorts() {
    return {BT::InputPort<std::string>("device_handle", "Device handle (provider_id/device_id)"),
            BT::InputPort<std::string>("signal_id", "Signal identifier"),
            BT::OutputPort<double>("value", "Signal value (as double)"),
            BT::OutputPort<std::string>("quality", "Signal quality (OK/STALE/UNAVAILABLE/FAULT)")};
}

BT::NodeStatus ReadSignalNode::tick() {
    auto state_cache = get_state_cache();
    int unused;  // to trigger clang-tidy warning to test
    if (!state_cache) {
        LOG_ERROR("[ReadSignalNode] StateCache not available in blackboard");
        return BT::NodeStatus::FAILURE;
    }

    // Get input ports
    auto device_handle = getInput<std::string>("device_handle");
    auto signal_id = getInput<std::string>("signal_id");

    if (!device_handle || !signal_id) {
        LOG_ERROR("[ReadSignalNode] Missing required input ports");
        return BT::NodeStatus::FAILURE;
    }

    // Read signal from StateCache
    auto signal_value = state_cache->get_signal_value(device_handle.value(), signal_id.value());

    if (!signal_value) {
        LOG_ERROR("[ReadSignalNode] Signal not found: " << device_handle.value() << "/" << signal_id.value());
        return BT::NodeStatus::FAILURE;
    }

    // Convert and write output ports
    double value_out = value_to_double(signal_value->value);
    std::string quality_out = quality_to_string(signal_value->quality);

    setOutput("value", value_out);
    setOutput("quality", quality_out);

    return BT::NodeStatus::SUCCESS;
}

state::StateCache *ReadSignalNode::get_state_cache() {
    auto blackboard = config().blackboard;
    if (!blackboard) return nullptr;

    auto ptr = blackboard->get<void *>("state_cache");
    if (!ptr) return nullptr;

    return static_cast<state::StateCache *>(ptr);
}

//-----------------------------------------------------------------------------
// CallDeviceNode
//-----------------------------------------------------------------------------

CallDeviceNode::CallDeviceNode(const std::string &name, const BT::NodeConfig &config)
    : BT::SyncActionNode(name, config) {}

BT::PortsList CallDeviceNode::providedPorts() {
    return {BT::InputPort<std::string>("device_handle", "Device handle (provider_id/device_id)"),
            BT::InputPort<std::string>("function_name", "Function identifier"),
            BT::InputPort<double>("arg_target", "Target argument (for functions like set_target_temp)"),
            BT::OutputPort<bool>("success", "Call result (true/false)"),
            BT::OutputPort<std::string>("error", "Error message if call failed")};
}

BT::NodeStatus CallDeviceNode::tick() {
    auto call_router = get_call_router();
    if (!call_router) {
        LOG_ERROR("[CallDeviceNode] CallRouter not available in blackboard");
        setOutput("success", false);
        setOutput("error", "CallRouter not available");
        return BT::NodeStatus::FAILURE;
    }

    // Get input ports
    auto device_handle = getInput<std::string>("device_handle");
    auto function_name = getInput<std::string>("function_name");

    if (!device_handle || !function_name) {
        LOG_ERROR("[CallDeviceNode] Missing required input ports");
        setOutput("success", false);
        setOutput("error", "Missing device_handle or function_name");
        return BT::NodeStatus::FAILURE;
    }

    // Build CallRequest
    // Note: For now, we don't parse arg_* ports dynamically.
    // This is a limitation - FIXME
    // or we'll add a helper method to extract all "arg_*" ports.
    control::CallRequest request;
    request.device_handle = device_handle.value();
    request.function_name = function_name.value();

    // TODO: Parse arg_* ports dynamically
    // For now, check for specific known arguments
    auto arg_target = getInput<double>("arg_target");
    if (arg_target) {
        request.args["target"] = double_to_value(arg_target.value());
    }

    auto arg_mode = getInput<std::string>("arg_mode");
    if (arg_mode) {
        anolis::deviceprovider::v0::Value val;
        val.set_type(anolis::deviceprovider::v0::VALUE_TYPE_STRING);
        val.set_string_value(arg_mode.value());
        request.args["mode"] = val;
    }

    // Get providers map from blackboard (fixed)
    auto providers = get_providers();
    if (!providers) {
        LOG_ERROR("[CallDeviceNode] Providers map not available in blackboard");
        setOutput("success", false);
        setOutput("error", "Providers not available");
        return BT::NodeStatus::FAILURE;
    }

    // Execute call via CallRouter
    auto result = call_router->execute_call(request, *providers);

    setOutput("success", result.success);
    setOutput("error", result.error_message);

    if (result.success) {
        LOG_INFO("[CallDeviceNode] Call succeeded: " << device_handle.value() << "/" << function_name.value());
        return BT::NodeStatus::SUCCESS;
    } else {
        LOG_ERROR("[CallDeviceNode] Call failed: " << result.error_message);
        return BT::NodeStatus::FAILURE;
    }

    return BT::NodeStatus::SUCCESS;
}

control::CallRouter *CallDeviceNode::get_call_router() {
    auto blackboard = config().blackboard;
    if (!blackboard) return nullptr;

    auto ptr = blackboard->get<void *>("call_router");
    if (!ptr) return nullptr;

    return static_cast<control::CallRouter *>(ptr);
}

std::unordered_map<std::string, std::shared_ptr<provider::IProviderHandle>> *CallDeviceNode::get_providers() {
    auto blackboard = config().blackboard;
    if (!blackboard) return nullptr;

    auto ptr = blackboard->get<void *>("providers");
    if (!ptr) return nullptr;

    return static_cast<std::unordered_map<std::string, std::shared_ptr<provider::IProviderHandle>> *>(ptr);
}

//-----------------------------------------------------------------------------
// CheckQualityNode
//-----------------------------------------------------------------------------

CheckQualityNode::CheckQualityNode(const std::string &name, const BT::NodeConfig &config)
    : BT::SyncActionNode(name, config) {}

BT::PortsList CheckQualityNode::providedPorts() {
    return {BT::InputPort<std::string>("device_handle", "Device handle (provider_id/device_id)"),
            BT::InputPort<std::string>("signal_id", "Signal identifier"),
            BT::InputPort<std::string>("expected_quality", "OK", "Expected quality (default: OK)")};
}

BT::NodeStatus CheckQualityNode::tick() {
    auto state_cache = get_state_cache();
    if (!state_cache) {
        LOG_ERROR("[CheckQualityNode] StateCache not available in blackboard");
        return BT::NodeStatus::FAILURE;
    }

    // Get input ports
    auto device_handle = getInput<std::string>("device_handle");
    auto signal_id = getInput<std::string>("signal_id");
    auto expected_quality_str = getInput<std::string>("expected_quality").value_or("OK");

    if (!device_handle || !signal_id) {
        LOG_ERROR("[CheckQualityNode] Missing required input ports");
        return BT::NodeStatus::FAILURE;
    }

    // Read signal from StateCache
    auto signal_value = state_cache->get_signal_value(device_handle.value(), signal_id.value());

    if (!signal_value) {
        LOG_ERROR("[CheckQualityNode] Signal not found: " << device_handle.value() << "/" << signal_id.value());
        return BT::NodeStatus::FAILURE;
    }

    // Check quality
    auto expected_quality = string_to_quality(expected_quality_str);

    if (signal_value->quality == expected_quality) {
        return BT::NodeStatus::SUCCESS;
    } else {
        LOG_INFO("[CheckQualityNode] Quality mismatch: expected " << expected_quality_str << ", got "
                                                                  << quality_to_string(signal_value->quality));
        return BT::NodeStatus::FAILURE;
    }
}

state::StateCache *CheckQualityNode::get_state_cache() {
    auto blackboard = config().blackboard;
    if (!blackboard) return nullptr;

    auto ptr = blackboard->get<void *>("state_cache");
    if (!ptr) return nullptr;

    return static_cast<state::StateCache *>(ptr);
}

//-----------------------------------------------------------------------------
// GetParameterNode
//-----------------------------------------------------------------------------

GetParameterNode::GetParameterNode(const std::string &name, const BT::NodeConfig &config)
    : BT::SyncActionNode(name, config) {}

/*static*/ BT::PortsList GetParameterNode::providedPorts() {
    return {BT::InputPort<std::string>("param", "Parameter name"),
            BT::OutputPort<double>("value", "Parameter value (as double, if applicable)")};
}

BT::NodeStatus GetParameterNode::tick() {
    auto parameter_manager = get_parameter_manager();
    if (!parameter_manager) {
        LOG_ERROR("[GetParameterNode] ParameterManager not available in blackboard");
        return BT::NodeStatus::FAILURE;
    }

    // Get input port
    auto param_name = getInput<std::string>("param");
    if (!param_name) {
        LOG_ERROR("[GetParameterNode] Missing 'param' input port");
        return BT::NodeStatus::FAILURE;
    }

    // Read parameter value
    auto value_opt = parameter_manager->get(param_name.value());
    if (!value_opt.has_value()) {
        LOG_ERROR("[GetParameterNode] Parameter not found: " << param_name.value());
        return BT::NodeStatus::FAILURE;
    }

    // Convert value to double for output port
    // Note: This is a simplification - in reality we'd need type-specific output ports
    // or a way to handle different types dynamically
    double output_value = 0.0;
    const auto &param_value = value_opt.value();

    if (std::holds_alternative<double>(param_value)) {
        output_value = std::get<double>(param_value);
    } else if (std::holds_alternative<int64_t>(param_value)) {
        output_value = static_cast<double>(std::get<int64_t>(param_value));
    } else if (std::holds_alternative<bool>(param_value)) {
        output_value = std::get<bool>(param_value) ? 1.0 : 0.0;
    } else if (std::holds_alternative<std::string>(param_value)) {
        // Cannot convert string to double, return failure
        LOG_ERROR("[GetParameterNode] Parameter '" << param_name.value() << "' is a string, cannot convert to double");
        return BT::NodeStatus::FAILURE;
    }

    setOutput("value", output_value);

    LOG_INFO("[GetParameterNode] Read parameter '" << param_name.value() << "' = " << output_value);

    return BT::NodeStatus::SUCCESS;
}

ParameterManager *GetParameterNode::get_parameter_manager() {
    auto blackboard = config().blackboard;
    if (!blackboard) return nullptr;

    auto ptr = blackboard->get<void *>("parameter_manager");
    if (!ptr) return nullptr;

    return static_cast<ParameterManager *>(ptr);
}

}  // namespace automation
}  // namespace anolis
