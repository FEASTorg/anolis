#pragma once

#include <memory>
#include <string>
#include <atomic>
#include <thread>

// BehaviorTree.CPP includes
#include <behaviortree_cpp/basic_types.h>

// BehaviorTree.CPP forward declarations  
namespace BT {
    class Tree;
    class BehaviorTreeFactory;
}

namespace anolis {

// Forward declarations
namespace state { class StateCache; }
namespace control { class CallRouter; }

namespace automation {

/**
 * BT Runtime - Phase 7A Foundation
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
     * Configuration for BT runtime
     */
    struct Config {
        bool enabled = false;                   // Whether automation is enabled
        std::string behavior_tree_path;         // Path to XML behavior tree file
        int tick_rate_hz = 10;                  // BT tick frequency (default 10 Hz)
        
        // Phase 7B additions (placeholders for now)
        // RuntimeMode initial_mode = RuntimeMode::MANUAL;
        // std::string manual_gating_policy = "BLOCK";
        
        // Phase 7C additions (placeholders for now)
        // bool persist_parameters = false;
    };

    /**
     * Construct BT runtime with kernel service dependencies.
     * 
     * @param state_cache Reference to state cache (for reading device state)
     * @param call_router Reference to call router (for device calls)
     */
    BTRuntime(state::StateCache& state_cache, control::CallRouter& call_router);
    
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
