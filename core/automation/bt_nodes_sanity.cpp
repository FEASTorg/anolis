#include <behaviortree_cpp/basic_types.h>
#include <behaviortree_cpp/bt_factory.h>

#include <iostream>

#include "automation/bt_nodes.hpp"

static bool has_port(const BT::PortsList &ports, const std::string &key) { return ports.find(key) != ports.end(); }

int main() {
    try {
        BT::BehaviorTreeFactory factory;
        factory.registerNodeType<anolis::automation::ReadSignalNode>("ReadSignal");
        factory.registerNodeType<anolis::automation::CallDeviceNode>("CallDevice");
        factory.registerNodeType<anolis::automation::CheckQualityNode>("CheckQuality");
        factory.registerNodeType<anolis::automation::GetParameterNode>("GetParameter");

        const auto read_ports = anolis::automation::ReadSignalNode::providedPorts();
        const auto call_ports = anolis::automation::CallDeviceNode::providedPorts();
        const auto qual_ports = anolis::automation::CheckQualityNode::providedPorts();
        const auto param_ports = anolis::automation::GetParameterNode::providedPorts();

        if (!has_port(read_ports, "device_handle") || !has_port(read_ports, "signal_id")) {
            std::cerr << "ReadSignalNode ports missing\n";
            return 1;
        }
        if (!has_port(call_ports, "device_handle") || !has_port(call_ports, "function_name") ||
            !has_port(call_ports, "arg_target")) {
            std::cerr << "CallDeviceNode ports missing\n";
            return 1;
        }
        if (!has_port(qual_ports, "device_handle") || !has_port(qual_ports, "signal_id")) {
            std::cerr << "CheckQualityNode ports missing\n";
            return 1;
        }
        if (!has_port(param_ports, "param") || !has_port(param_ports, "value")) {
            std::cerr << "GetParameterNode ports missing\n";
            return 1;
        }

        std::cout << "bt_nodes_sanity: PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "bt_nodes_sanity: EXCEPTION: " << e.what() << "\n";
        return 2;
    }
}
