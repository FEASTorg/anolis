#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

// BehaviorTree.CPP includes
#include <behaviortree_cpp/basic_types.h>

// BehaviorTree.CPP forward declarations
namespace BT {
class Tree;
class BehaviorTreeFactory;
}  // namespace BT

namespace anolis {

// Forward declarations
namespace state {
class StateCache;
}
namespace control {
class CallRouter;
}
namespace provider {
class IProviderHandle;
class ProviderRegistry;
}  // namespace provider

namespace automation {

class ModeManager;
class ParameterManager;

/**
 * BT Runtime
 *
 * Manages Behavior Tree lifecycle for the Anolis automation layer.
 *
 * Architecture constraints:
 * - BT nodes read ONLY via StateCache (no direct provider access)
 * - BT nodes act ONLY via CallRouter (validated control path)
 * - Single-threaded tick loop in dedicated thread
 * - Tick rate configurable (default 10 Hz)
 *
 * The BT engine is a CONSUMER of kernel services (StateCache, CallRouter),
 * not a new subsystem layered beneath them.
 */
class BTRuntime {
public:
    /**
     * Construct BT runtime with kernel service dependencies.
     *
     * @param state_cache Reference to state cache (for reading device state)
     * @param call_router Reference to call router (for device calls)
     * @param provider_registry Provider registry (for CallRouter::execute_call)
     * @param mode_manager Mode state machine (for AUTO/MANUAL gating)
     * @param parameter_manager Parameter manager (nullptr if not used)
     */
    BTRuntime(state::StateCache& state_cache, control::CallRouter& call_router,
              provider::ProviderRegistry& provider_registry, ModeManager& mode_manager,
              ParameterManager* parameter_manager = nullptr);

    ~BTRuntime();

    // Non-copyable, non-movable (manages thread)
    BTRuntime(const BTRuntime&) = delete;
    BTRuntime& operator=(const BTRuntime&) = delete;
    BTRuntime(BTRuntime&&) = delete;
    BTRuntime& operator=(BTRuntime&&) = delete;

    /**
     * Load behavior tree from XML file.
     *
     * @param path Path to BehaviorTree.CPP XML file
     * @return true if loaded successfully, false otherwise
     */
    bool load_tree(const std::string& path);

    /**
     * Start BT tick loop in dedicated thread.
     *
     * @param tick_rate_hz Tick frequency in Hz (e.g., 10 = 100ms period)
     * @return true if started successfully, false if already running or not loaded
     */
    bool start(int tick_rate_hz = 10);

    /**
     * Stop BT tick loop gracefully.
     * Waits for current tick to complete, then halts thread.
     */
    void stop();

    /**
     * Check if BT is currently running.
     */
    bool is_running() const;

    /**
     * Execute a single BT tick (for testing or manual control).
     *
     * Note: Normally called by tick loop thread, but exposed for unit testing.
     *
     * @return BT::NodeStatus (SUCCESS, FAILURE, RUNNING)
     */
    BT::NodeStatus tick();

private:
    /**
     * Tick loop thread function.
     * Runs continuously at configured rate until stop() is called.
     */
    void tick_loop();

    /**
     * Populate BT blackboard with StateCache snapshot.
     * Called before each tick to ensure consistent state view.
     */
    void populate_blackboard();

    // Kernel service references (non-owning)
    state::StateCache& state_cache_;
    control::CallRouter& call_router_;
    provider::ProviderRegistry& provider_registry_;
    ModeManager& mode_manager_;
    ParameterManager* parameter_manager_;  // nullable

    // BT state
    std::unique_ptr<BT::BehaviorTreeFactory> factory_;
    std::unique_ptr<BT::Tree> tree_;
    std::string tree_path_;
    bool tree_loaded_ = false;

    // Threading
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> tick_thread_;
    int tick_rate_hz_ = 10;
};

}  // namespace automation
}  // namespace anolis
