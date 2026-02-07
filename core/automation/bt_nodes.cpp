#include "automation/bt_nodes.hpp"

#include <iostream>
#include <nlohmann/json.hpp>
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
    if (s == "OK") {
        return anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_OK;
    }
    if (s == "STALE") {
        return anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_STALE;
    }
    if (s == "FAULT") {
        return anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_FAULT;
    }
    if (s == "UNKNOWN") {
        return anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_UNKNOWN;
    }
    return anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_UNSPECIFIED;
}

// Helper: Create protobuf Value from double
static anolis::deviceprovider::v0::Value double_to_value(double d) {
    anolis::deviceprovider::v0::Value val;
    val.set_type(anolis::deviceprovider::v0::VALUE_TYPE_DOUBLE);
    val.set_double_value(d);
    return val;
}

// Helper: Create protobuf Value from int64
static anolis::deviceprovider::v0::Value int64_to_value(int64_t i) {
    anolis::deviceprovider::v0::Value val;
    val.set_type(anolis::deviceprovider::v0::VALUE_TYPE_INT64);
    val.set_int64_value(i);
    return val;
}

// Helper: Create protobuf Value from bool
static anolis::deviceprovider::v0::Value bool_to_value(bool b) {
    anolis::deviceprovider::v0::Value val;
    val.set_type(anolis::deviceprovider::v0::VALUE_TYPE_BOOL);
    val.set_bool_value(b);
    return val;
}

// Helper: Create protobuf Value from string
static anolis::deviceprovider::v0::Value string_to_value(const std::string &s) {
    anolis::deviceprovider::v0::Value val;
    val.set_type(anolis::deviceprovider::v0::VALUE_TYPE_STRING);
    val.set_string_value(s);
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
    if (state_cache == nullptr) {
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
    if (blackboard == nullptr) {
        return nullptr;
    }

    auto ptr = blackboard->get<void *>("state_cache");
    if (ptr == nullptr) {
        return nullptr;
    }

    return static_cast<state::StateCache *>(ptr);
}

//-----------------------------------------------------------------------------
// CallDeviceNode
//-----------------------------------------------------------------------------

CallDeviceNode::CallDeviceNode(const std::string &name, const BT::NodeConfig &config)
    : BT::SyncActionNode(name, config) {}

BT::PortsList CallDeviceNode::providedPorts() {
    // Structured payload interface: device_handle + function_name + args (JSON)
    // Args is a JSON object string, e.g., '{"target":30.0}'
    // Validation handled by CallRouter using ArgSpec metadata
    return {BT::InputPort<std::string>("device_handle", "Device handle (provider_id/device_id)"),
            BT::InputPort<std::string>("function_name", "Function identifier"),
            BT::InputPort<std::string>("args", "{}", "Arguments as JSON object (e.g., '{\"target\":30.0}')"),
            BT::OutputPort<bool>("success", "Call result (true/false)"),
            BT::OutputPort<std::string>("error", "Error message if call failed")};
}

BT::NodeStatus CallDeviceNode::tick() {
    auto call_router = get_call_router();
    if (call_router == nullptr) {
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
    control::CallRequest request;
    request.device_handle = device_handle.value();
    request.function_name = function_name.value();

    // Parse args JSON and convert to protobuf Value map
    auto args_str = getInput<std::string>("args").value_or("{}");
    if (!args_str.empty() && args_str != "{}") {
        try {
            auto json_args = nlohmann::json::parse(args_str);
            if (!json_args.is_object()) {
                LOG_ERROR("[CallDeviceNode] args must be a JSON object, got: " << args_str);
                setOutput("success", false);
                setOutput("error", "args must be a JSON object");
                return BT::NodeStatus::FAILURE;
            }
            for (auto &[key, value] : json_args.items()) {
                if (value.is_number_float()) {
                    request.args[key] = double_to_value(value.get<double>());
                } else if (value.is_number_integer()) {
                    // JSON integers could be int or uint - use int64 as safe default
                    request.args[key] = int64_to_value(value.get<int64_t>());
                } else if (value.is_boolean()) {
                    request.args[key] = bool_to_value(value.get<bool>());
                } else if (value.is_string()) {
                    request.args[key] = string_to_value(value.get<std::string>());
                } else {
                    LOG_ERROR("[CallDeviceNode] Unsupported JSON type for arg '" << key << "'");
                    setOutput("success", false);
                    setOutput("error", "Unsupported JSON type for arg '" + key + "'");
                    return BT::NodeStatus::FAILURE;
                }
            }
        } catch (const nlohmann::json::parse_error &e) {
            LOG_ERROR("[CallDeviceNode] JSON parse error: " << e.what());
            setOutput("success", false);
            setOutput("error", std::string("JSON parse error: ") + e.what());
            return BT::NodeStatus::FAILURE;
        }
    }

    // Get provider registry from blackboard
    auto provider_registry = get_provider_registry();
    if (provider_registry == nullptr) {
        LOG_ERROR("[CallDeviceNode] ProviderRegistry not available in blackboard");
        setOutput("success", false);
        setOutput("error", "Provider registry not available");
        return BT::NodeStatus::FAILURE;
    }

    // Execute call via CallRouter
    auto result = call_router->execute_call(request, *provider_registry);

    setOutput("success", result.success);
    setOutput("error", result.error_message);

    if (result.success) {
        LOG_INFO("[CallDeviceNode] Call succeeded: " << device_handle.value() << "/" << function_name.value());
        return BT::NodeStatus::SUCCESS;
    }

    LOG_ERROR("[CallDeviceNode] Call failed: " << result.error_message);
    return BT::NodeStatus::FAILURE;
}

// Template implementation for typed blackboard access
template <typename T>
T *CallDeviceNode::get_service(const std::string &key) {
    auto blackboard = config().blackboard;
    if (blackboard == nullptr) {
        return nullptr;
    }

    auto ptr = blackboard->get<void *>(key);
    if (ptr == nullptr) {
        return nullptr;
    }

    return static_cast<T *>(ptr);
}

control::CallRouter *CallDeviceNode::get_call_router() { return get_service<control::CallRouter>("call_router"); }

provider::ProviderRegistry *CallDeviceNode::get_provider_registry() {
    return get_service<provider::ProviderRegistry>("provider_registry");
}

//-----------------------------------------------------------------------------
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
    if (state_cache == nullptr) {
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
    }

    LOG_INFO("[CheckQualityNode] Quality mismatch: expected " << expected_quality_str << ", got "
                                                              << quality_to_string(signal_value->quality));
    return BT::NodeStatus::FAILURE;
}

state::StateCache *CheckQualityNode::get_state_cache() {
    auto blackboard = config().blackboard;
    if (blackboard == nullptr) {
        return nullptr;
    }

    auto ptr = blackboard->get<void *>("state_cache");
    if (ptr == nullptr) {
        return nullptr;
    }

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
