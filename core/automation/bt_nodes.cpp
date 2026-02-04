#include "automation/bt_nodes.hpp"
#include "state/state_cache.hpp"
#include "control/call_router.hpp"

#include <iostream>
#include <sstream>

namespace anolis {
namespace automation {

// Helper: Convert protobuf Value to double (for output port)
static double value_to_double(const anolis::deviceprovider::v0::Value& val) {
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
static std::string value_to_string(const anolis::deviceprovider::v0::Value& val) {
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
static anolis::deviceprovider::v0::SignalValue_Quality string_to_quality(const std::string& s) {
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

ReadSignalNode::ReadSignalNode(const std::string& name, const BT::NodeConfig& config)
    : BT::SyncActionNode(name, config)
{}

BT::PortsList ReadSignalNode::providedPorts() {
    return {
        BT::InputPort<std::string>("device_handle", "Device handle (provider_id/device_id)"),
        BT::InputPort<std::string>("signal_id", "Signal identifier"),
        BT::OutputPort<double>("value", "Signal value (as double)"),
        BT::OutputPort<std::string>("quality", "Signal quality (OK/STALE/UNAVAILABLE/FAULT)")
    };
}

BT::NodeStatus ReadSignalNode::tick() {
    auto state_cache = get_state_cache();
    if (!state_cache) {
        std::cerr << "[ReadSignalNode] ERROR: StateCache not available in blackboard" << std::endl;
        return BT::NodeStatus::FAILURE;
    }
    
    // Get input ports
    auto device_handle = getInput<std::string>("device_handle");
    auto signal_id = getInput<std::string>("signal_id");
    
    if (!device_handle || !signal_id) {
        std::cerr << "[ReadSignalNode] ERROR: Missing required input ports" << std::endl;
        return BT::NodeStatus::FAILURE;
    }
    
    // Read signal from StateCache
    auto signal_value = state_cache->get_signal_value(device_handle.value(), signal_id.value());
    
    if (!signal_value) {
        std::cerr << "[ReadSignalNode] ERROR: Signal not found: " 
                  << device_handle.value() << "/" << signal_id.value() << std::endl;
        return BT::NodeStatus::FAILURE;
    }
    
    // Convert and write output ports
    double value_out = value_to_double(signal_value->value);
    std::string quality_out = quality_to_string(signal_value->quality);
    
    setOutput("value", value_out);
    setOutput("quality", quality_out);
    
    return BT::NodeStatus::SUCCESS;
}

state::StateCache* ReadSignalNode::get_state_cache() {
    auto blackboard = config().blackboard;
    if (!blackboard) return nullptr;
    
    auto ptr = blackboard->get<void*>("state_cache");
    if (!ptr) return nullptr;
    
    return static_cast<state::StateCache*>(ptr);
}

//-----------------------------------------------------------------------------
// CallDeviceNode
//-----------------------------------------------------------------------------

CallDeviceNode::CallDeviceNode(const std::string& name, const BT::NodeConfig& config)
    : BT::SyncActionNode(name, config)
{}

BT::PortsList CallDeviceNode::providedPorts() {
    return {
        BT::InputPort<std::string>("device_handle", "Device handle (provider_id/device_id)"),
        BT::InputPort<std::string>("function_name", "Function identifier"),
        // Note: Arguments are accessed dynamically via getInput("arg_*")
        BT::OutputPort<bool>("success", "Call result (true/false)"),
        BT::OutputPort<std::string>("error", "Error message if call failed")
    };
}

BT::NodeStatus CallDeviceNode::tick() {
    auto call_router = get_call_router();
    if (!call_router) {
        std::cerr << "[CallDeviceNode] ERROR: CallRouter not available in blackboard" << std::endl;
        setOutput("success", false);
        setOutput("error", "CallRouter not available");
        return BT::NodeStatus::FAILURE;
    }
    
    // Get input ports
    auto device_handle = getInput<std::string>("device_handle");
    auto function_name = getInput<std::string>("function_name");
    
    if (!device_handle || !function_name) {
        std::cerr << "[CallDeviceNode] ERROR: Missing required input ports" << std::endl;
        setOutput("success", false);
        setOutput("error", "Missing device_handle or function_name");
        return BT::NodeStatus::FAILURE;
    }
    
    // Build CallRequest
    // Note: For now, we don't parse arg_* ports dynamically.
    // This is a limitation - Phase 7A.6 demo will use a simpler approach
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
    
    // Note: We don't have access to providers map here!
    // This is a problem - CallRouter::execute_call requires providers reference.
    // 
    // Solution: We'll need to store providers reference in blackboard too,
    // or refactor CallRouter to not require it as a parameter.
    //
    // For Phase 7A.3, we'll document this limitation and address in 7A.5
    // when integrating with Runtime (which has providers map).
    
    std::cerr << "[CallDeviceNode] WARNING: Providers map not accessible from BT node" << std::endl;
    std::cerr << "[CallDeviceNode] Call validation only (no execution)" << std::endl;
    
    // Validate call only
    std::string error_msg;
    bool valid = call_router->validate_call(request, error_msg);
    
    if (!valid) {
        std::cerr << "[CallDeviceNode] ERROR: Call validation failed: " << error_msg << std::endl;
        setOutput("success", false);
        setOutput("error", error_msg);
        return BT::NodeStatus::FAILURE;
    }
    
    setOutput("success", true);
    setOutput("error", "");
    
    std::cout << "[CallDeviceNode] Call validated (execution deferred to Phase 7A.5): " 
              << device_handle.value() << "/" << function_name.value() << std::endl;
    
    return BT::NodeStatus::SUCCESS;
}

control::CallRouter* CallDeviceNode::get_call_router() {
    auto blackboard = config().blackboard;
    if (!blackboard) return nullptr;
    
    auto ptr = blackboard->get<void*>("call_router");
    if (!ptr) return nullptr;
    
    return static_cast<control::CallRouter*>(ptr);
}

//-----------------------------------------------------------------------------
// CheckQualityNode
//-----------------------------------------------------------------------------

CheckQualityNode::CheckQualityNode(const std::string& name, const BT::NodeConfig& config)
    : BT::SyncActionNode(name, config)
{}

BT::PortsList CheckQualityNode::providedPorts() {
    return {
        BT::InputPort<std::string>("device_handle", "Device handle (provider_id/device_id)"),
        BT::InputPort<std::string>("signal_id", "Signal identifier"),
        BT::InputPort<std::string>("expected_quality", "OK", "Expected quality (default: OK)")
    };
}

BT::NodeStatus CheckQualityNode::tick() {
    auto state_cache = get_state_cache();
    if (!state_cache) {
        std::cerr << "[CheckQualityNode] ERROR: StateCache not available in blackboard" << std::endl;
        return BT::NodeStatus::FAILURE;
    }
    
    // Get input ports
    auto device_handle = getInput<std::string>("device_handle");
    auto signal_id = getInput<std::string>("signal_id");
    auto expected_quality_str = getInput<std::string>("expected_quality").value_or("OK");
    
    if (!device_handle || !signal_id) {
        std::cerr << "[CheckQualityNode] ERROR: Missing required input ports" << std::endl;
        return BT::NodeStatus::FAILURE;
    }
    
    // Read signal from StateCache
    auto signal_value = state_cache->get_signal_value(device_handle.value(), signal_id.value());
    
    if (!signal_value) {
        std::cerr << "[CheckQualityNode] Signal not found: " 
                  << device_handle.value() << "/" << signal_id.value() << std::endl;
        return BT::NodeStatus::FAILURE;
    }
    
    // Check quality
    auto expected_quality = string_to_quality(expected_quality_str);
    
    if (signal_value->quality == expected_quality) {
        return BT::NodeStatus::SUCCESS;
    } else {
        std::cout << "[CheckQualityNode] Quality mismatch: expected " 
                  << expected_quality_str << ", got " 
                  << quality_to_string(signal_value->quality) << std::endl;
        return BT::NodeStatus::FAILURE;
    }
}

state::StateCache* CheckQualityNode::get_state_cache() {
    auto blackboard = config().blackboard;
    if (!blackboard) return nullptr;
    
    auto ptr = blackboard->get<void*>("state_cache");
    if (!ptr) return nullptr;
    
    return static_cast<state::StateCache*>(ptr);
}

}  // namespace automation
}  // namespace anolis
