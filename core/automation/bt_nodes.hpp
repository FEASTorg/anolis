#pragma once

#include <behaviortree_cpp/action_node.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "automation/bt_services.hpp"

namespace anolis {

// Forward declarations
namespace state {
class StateCache;
}
namespace control {
class CallRouter;
}
namespace provider {
class ProviderRegistry;
}  // namespace provider

namespace automation {

class ParameterManager;

/**
 * ReadSignalNode - BT action node for reading device signals
 *
 * Reads a signal value from StateCache via blackboard.
 *
 * XML Port Configuration:
 * - device_handle (input): "provider_id/device_id"
 * - signal_id (input): Signal identifier
 * - value (output): Signal value (double/int/bool/string)
 * - quality (output): Signal quality ("OK", "STALE", "UNAVAILABLE", "FAULT")
 *
 * Returns:
 * - SUCCESS: Signal read successfully, value/quality written to outputs
 * - FAILURE: Signal not found or StateCache unavailable
 *
 * Example XML:
 * <ReadSignal device_handle="sim/tempctl0" signal_id="tc1_temp"
 *             value="{temp_value}" quality="{temp_quality}"/>
 */
class ReadSignalNode : public BT::SyncActionNode {
public:
    ReadSignalNode(const std::string &name, const BT::NodeConfig &config);

    BT::NodeStatus tick() override;

    static BT::PortsList providedPorts();

private:
    std::optional<BTServiceContext> get_services() const;
};

/**
 * CallDeviceNode - BT action node for calling device functions
 *
 * Invokes a device function via CallRouter with arguments.
 *
 * XML Port Configuration:
 * - device_handle (input): "provider_id/device_id"
 * - function_name (input): Function identifier
 * - args (input, optional): JSON object with function arguments (e.g., '{"target":25.0}')
 * - success (output): Call result (true/false)
 * - error (output): Error message if call failed
 *
 * Returns:
 * - SUCCESS: Function executed successfully
 * - FAILURE: Function call failed (precondition violated, provider unavailable, etc.)
 *
 * Example XML:
 * <CallDevice device_handle="sim/tempctl0" function_name="set_target_temp"
 *             args='{"target":25.0}' success="{call_success}" error="{call_error}"/>
 *
 * Note: This node may BLOCK during device call execution. Design BTs accordingly.
 */
class CallDeviceNode : public BT::SyncActionNode {
public:
    CallDeviceNode(const std::string &name, const BT::NodeConfig &config);

    BT::NodeStatus tick() override;

    static BT::PortsList providedPorts();

private:
    std::optional<BTServiceContext> get_services() const;
};

/**
 * CheckQualityNode - BT condition node for verifying signal quality
 *
 * Checks if a signal's quality meets expectations.
 *
 * XML Port Configuration:
 * - device_handle (input): "provider_id/device_id"
 * - signal_id (input): Signal identifier
 * - expected_quality (input, optional): Expected quality ("OK", default)
 *
 * Returns:
 * - SUCCESS: Signal quality matches expected
 * - FAILURE: Signal quality does not match or signal not found
 *
 * Example XML:
 * <CheckQuality device_handle="sim/tempctl0" signal_id="tc1_temp"
 *               expected_quality="OK"/>
 *
 * This is useful for gating control actions on sensor availability:
 * <Sequence>
 *   <CheckQuality device_handle="sim/tempctl0" signal_id="tc1_temp"/>
 *   <CallDevice device_handle="sim/tempctl0" function_name="set_target_temp"
 *               args='{"target":30.0}'/>
 * </Sequence>
 */
class CheckQualityNode : public BT::SyncActionNode {
public:
    CheckQualityNode(const std::string &name, const BT::NodeConfig &config);

    BT::NodeStatus tick() override;

    static BT::PortsList providedPorts();

private:
    std::optional<BTServiceContext> get_services() const;
};

/**
 * GetParameterNode - BT action node for reading runtime parameters
 *
 * Reads a parameter value from ParameterManager via blackboard.
 *
 * XML Port Configuration:
 * - param (input): Parameter name
 * - value (output): Parameter value (numeric only:
 * double/int64)
 * Returns:
 * - SUCCESS: Parameter read successfully, value written to output
 * - FAILURE: Parameter not found, ParameterManager
 * unavailable, or non-numeric type
 * Example XML: <GetParameter param="temp_setpoint" value="{target_temp}"/>
 *
 * Note: To use parameter values with CallDevice, construct JSON args using Script nodes
 * or use static values directly in the args attribute.
 */
class GetParameterNode : public BT::SyncActionNode {
public:
    GetParameterNode(const std::string &name, const BT::NodeConfig &config);

    BT::NodeStatus tick() override;

    static BT::PortsList providedPorts();

private:
    std::optional<BTServiceContext> get_services() const;
};

}  // namespace automation
}  // namespace anolis
