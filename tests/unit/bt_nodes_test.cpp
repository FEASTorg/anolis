#include <gtest/gtest.h>

#include <behaviortree_cpp/blackboard.h>
#include <behaviortree_cpp/bt_factory.h>

#include <cstdint>
#include <string>

#include "automation/bt_nodes.hpp"
#include "automation/bt_services.hpp"
#include "automation/parameter_manager.hpp"
#include "control/call_router.hpp"
#include "provider/provider_registry.hpp"
#include "registry/device_registry.hpp"
#include "state/state_cache.hpp"

namespace {

std::string make_get_parameter_tree_xml(const std::string& param_name) {
    return std::string(R"(<?xml version="1.0"?>
<root BTCPP_format="4">
  <BehaviorTree ID="MainTree">
    <GetParameter param=")")
        + param_name +
        R"(" value="{out}"/>
  </BehaviorTree>
</root>
)";
}

class BTNodesTest : public ::testing::Test {
protected:
    anolis::registry::DeviceRegistry registry_;
    anolis::state::StateCache state_cache_{registry_, 100};
    anolis::control::CallRouter call_router_{registry_, state_cache_};
    anolis::provider::ProviderRegistry provider_registry_;
    anolis::automation::ParameterManager parameter_manager_;

    static void register_nodes(BT::BehaviorTreeFactory& factory) {
        factory.registerNodeType<anolis::automation::GetParameterNode>("GetParameter");
    }

    BT::Blackboard::Ptr make_blackboard(bool with_context) {
        auto blackboard = BT::Blackboard::create();
        if (with_context) {
            anolis::automation::BTServiceContext services;
            services.state_cache = &state_cache_;
            services.call_router = &call_router_;
            services.provider_registry = &provider_registry_;
            services.parameter_manager = &parameter_manager_;
            blackboard->set(anolis::automation::kBTServiceContextKey, services);
        }
        return blackboard;
    }
};

}  // namespace

TEST_F(BTNodesTest, GetParameterNodeSucceedsForDouble) {
    ASSERT_TRUE(parameter_manager_.define("temp_setpoint", anolis::automation::ParameterType::DOUBLE, 33.5));

    BT::BehaviorTreeFactory factory;
    register_nodes(factory);
    auto blackboard = make_blackboard(true);
    auto tree = factory.createTreeFromText(make_get_parameter_tree_xml("temp_setpoint"), blackboard);

    EXPECT_EQ(BT::NodeStatus::SUCCESS, tree.tickOnce());
    EXPECT_DOUBLE_EQ(33.5, blackboard->get<double>("out"));
}

TEST_F(BTNodesTest, GetParameterNodeSucceedsForInt64) {
    ASSERT_TRUE(parameter_manager_.define("retry_limit", anolis::automation::ParameterType::INT64, int64_t{7}));

    BT::BehaviorTreeFactory factory;
    register_nodes(factory);
    auto blackboard = make_blackboard(true);
    auto tree = factory.createTreeFromText(make_get_parameter_tree_xml("retry_limit"), blackboard);

    EXPECT_EQ(BT::NodeStatus::SUCCESS, tree.tickOnce());
    EXPECT_DOUBLE_EQ(7.0, blackboard->get<double>("out"));
}

TEST_F(BTNodesTest, GetParameterNodeFailsForNonNumericTypes) {
    ASSERT_TRUE(parameter_manager_.define("enabled", anolis::automation::ParameterType::BOOL, true));
    ASSERT_TRUE(parameter_manager_.define("mode_name", anolis::automation::ParameterType::STRING, std::string("AUTO")));

    BT::BehaviorTreeFactory factory;
    register_nodes(factory);

    auto bool_blackboard = make_blackboard(true);
    auto bool_tree = factory.createTreeFromText(make_get_parameter_tree_xml("enabled"), bool_blackboard);
    EXPECT_EQ(BT::NodeStatus::FAILURE, bool_tree.tickOnce());

    auto string_blackboard = make_blackboard(true);
    auto string_tree = factory.createTreeFromText(make_get_parameter_tree_xml("mode_name"), string_blackboard);
    EXPECT_EQ(BT::NodeStatus::FAILURE, string_tree.tickOnce());
}

TEST_F(BTNodesTest, GetParameterNodeFailsWhenServiceContextMissing) {
    ASSERT_TRUE(parameter_manager_.define("temp_setpoint", anolis::automation::ParameterType::DOUBLE, 21.0));

    BT::BehaviorTreeFactory factory;
    register_nodes(factory);
    auto blackboard = make_blackboard(false);
    auto tree = factory.createTreeFromText(make_get_parameter_tree_xml("temp_setpoint"), blackboard);

    EXPECT_EQ(BT::NodeStatus::FAILURE, tree.tickOnce());
}
