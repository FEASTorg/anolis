#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

// BehaviorTree.CPP includes
#include <behaviortree_cpp/basic_types.h>

#include "automation/bt_services.hpp"

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
namespace events {
class EventEmitter;
}  // namespace events

namespace automation {

class ModeManager;
class ParameterManager;

/**
 * Automation Health Status
 */
enum class BTStatus {
    BT_IDLE,     // No tree loaded or not running
    BT_RUNNING,  // Tree executing normally
    BT_STALLED,  // Tree returning FAILURE for multiple ticks
    BT_ERROR     // Critical error (e.g., exception during tick)
};

/**
 * Automation Health Information
 */
struct AutomationHealth {
    BTStatus bt_status = BTStatus::BT_IDLE;
    uint64_t last_tick_ms = 0;
    uint64_t ticks_since_progress = 0;
    uint64_t total_ticks = 0;
    std::string last_error;
    uint64_t error_count = 0;
    std::string current_tree;
};

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

    /**
     * Get the path to the currently loaded BT file.
     *
     * @return Path to loaded BT XML file, or empty string if not loaded
     */
    const std::string& get_tree_path() const { return tree_path_; }

    /**
     * Get current automation health status.
     *
     * @return AutomationHealth struct with current health metrics
     */
    AutomationHealth get_health() const;

    /**
     * @brief Set event emitter for error notifications
     *
     * When set, BTRuntime will emit BTErrorEvent on failures.
     * Must be called before start().
     *
     * @param emitter Shared pointer to EventEmitter (can be nullptr to disable)
     */
    void set_event_emitter(const std::shared_ptr<events::EventEmitter>& emitter);

private:
    /**
     * Tick loop thread function.
     * Runs continuously at configured rate until stop() is called.
     */
    void tick_loop();

    /**
     * Populate BT blackboard with typed kernel service references.
     * Called before ticking to keep direct
     * tick() and threaded mode consistent.
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

    // Health tracking
    mutable std::mutex health_mutex_;
    uint64_t last_tick_ms_ = 0;
    BT::NodeStatus last_tick_status_ = BT::NodeStatus::IDLE;
    uint64_t ticks_since_progress_ = 0;
    uint64_t total_ticks_ = 0;
    std::string last_error_;
    uint64_t error_count_ = 0;

    // Event emitter (optional, for error notifications)
    std::shared_ptr<events::EventEmitter> event_emitter_;
    std::atomic<uint64_t> next_event_id_{1};
};

}  // namespace automation
}  // namespace anolis
