#pragma once

namespace anolis {
namespace state {
class StateCache;
}
namespace control {
class CallRouter;
}
namespace provider {
class ProviderRegistry;
}
namespace automation {

class ParameterManager;

// Typed blackboard payload consumed by all custom BT nodes.
struct BTServiceContext {
    state::StateCache* state_cache = nullptr;
    control::CallRouter* call_router = nullptr;
    provider::ProviderRegistry* provider_registry = nullptr;
    ParameterManager* parameter_manager = nullptr;
};

inline constexpr const char* kBTServiceContextKey = "anolis.bt_service_context";

}  // namespace automation
}  // namespace anolis
