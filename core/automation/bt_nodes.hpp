#pragma once

#include <behaviortree_cpp/action_node.h>
#include <string>
#include <memory>
#include <unordered_map>

namespace anolis
{

    // Forward declarations
    namespace state
    {
        class StateCache;
    }
    namespace control
    {
        class CallRouter;
    }
    namespace provider
    {
        class IProviderHandle;
    }

    namespace automation
    {

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
        class ReadSignalNode : public BT::SyncActionNode
        {
        public:
            ReadSignalNode(const std::string &name, const BT::NodeConfig &config);

            BT::NodeStatus tick() override;

            static BT::PortsList providedPorts();

        private:
            state::StateCache *get_state_cache();
        };

        /**
         * CallDeviceNode - BT action node for calling device functions
         *
         * Invokes a device function via CallRouter with arguments.
         *
         * XML Port Configuration:
         * - device_handle (input): "provider_id/device_id"
         * - function_name (input): Function identifier
         * - arg_* (input, optional): Function arguments (e.g., arg_target: 25.0)
         * - success (output): Call result (true/false)
         * - error (output): Error message if call failed
         *
         * Returns:
         * - SUCCESS: Function executed successfully
         * - FAILURE: Function call failed (precondition violated, provider unavailable, etc.)
         *
         * Example XML:
         * <CallDevice device_handle="sim/tempctl0" function_name="set_target_temp"
         *             arg_target="25.0" success="{call_success}" error="{call_error}"/>
         *
         * Note: This node may BLOCK during device call execution. Design BTs accordingly.
         */
        class CallDeviceNode : public BT::SyncActionNode
        {
        public:
            CallDeviceNode(const std::string &name, const BT::NodeConfig &config);

            BT::NodeStatus tick() override;

            static BT::PortsList providedPorts();

        private:
            control::CallRouter *get_call_router();
            std::unordered_map<std::string, std::shared_ptr<provider::IProviderHandle>> *get_providers();
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
         *   <CallDevice device_handle="sim/tempctl0" function_name="set_target_temp" arg_target="30.0"/>
         * </Sequence>
         */
        class CheckQualityNode : public BT::SyncActionNode
        {
        public:
            CheckQualityNode(const std::string &name, const BT::NodeConfig &config);

            BT::NodeStatus tick() override;

            static BT::PortsList providedPorts();

        private:
            state::StateCache *get_state_cache();
        };

        /**
         * GetParameterNode - BT action node for reading runtime parameters
         *
         * Reads a parameter value from ParameterManager via blackboard.
         *
         * XML Port Configuration:
         * - param (input): Parameter name
         * - value (output): Parameter value (double/int64/bool/string)
         *
         * Returns:
         * - SUCCESS: Parameter read successfully, value written to output
         * - FAILURE: Parameter not found or ParameterManager unavailable
         *
         * Example XML:
         * <GetParameter param="temp_setpoint" value="{target_temp}"/>
         * <CallDevice device_handle="sim/tempctl0" function_name="set_target_temp"
         *             arg_target="{target_temp}"/>
         */
        class GetParameterNode : public BT::SyncActionNode
        {
        public:
            GetParameterNode(const std::string &name, const BT::NodeConfig &config);

            BT::NodeStatus tick() override;

            static BT::PortsList providedPorts();

        private:
            ParameterManager *get_parameter_manager();
        };

    } // namespace automation
} // namespace anolis
