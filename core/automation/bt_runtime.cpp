#include "automation/bt_runtime.hpp"

#include <behaviortree_cpp/blackboard.h>
#include <behaviortree_cpp/bt_factory.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

#include "automation/bt_nodes.hpp"
#include "automation/mode_manager.hpp"
#include "automation/parameter_manager.hpp"
#include "control/call_router.hpp"
#include "logging/logger.hpp"
#include "state/state_cache.hpp"

namespace anolis {
namespace automation {

BTRuntime::BTRuntime(state::StateCache &state_cache, control::CallRouter &call_router,
                     provider::ProviderRegistry &provider_registry, ModeManager &mode_manager,
                     ParameterManager *parameter_manager)
    : state_cache_(state_cache),
      call_router_(call_router),
      provider_registry_(provider_registry),
      mode_manager_(mode_manager),
      parameter_manager_(parameter_manager),
      factory_(std::make_unique<BT::BehaviorTreeFactory>()) {
    LOG_INFO("[BTRuntime] Initialized");

    // Register custom nodes
    factory_->registerNodeType<ReadSignalNode>("ReadSignal");
    factory_->registerNodeType<CallDeviceNode>("CallDevice");
    factory_->registerNodeType<CheckQualityNode>("CheckQuality");
    factory_->registerNodeType<GetParameterNode>("GetParameter");

    LOG_INFO("[BTRuntime] Registered custom node types");
}

BTRuntime::~BTRuntime() { stop(); }

bool BTRuntime::load_tree(const std::string &path) {
    // Verify file exists
    std::ifstream file(path);
    if (!file.good()) {
        LOG_ERROR("[BTRuntime] Cannot open BT file: " << path);
        return false;
    }

    tree_path_ = path;

    // MUST populate blackboard before creating tree!
    // BT nodes access blackboard during construction/initialization
    populate_blackboard();

    try {
        tree_ = std::make_unique<BT::Tree>(factory_->createTreeFromFile(path));
        tree_loaded_ = true;

        LOG_INFO("[BTRuntime] BT loaded successfully: " << path);
        return true;
    } catch (const std::exception &e) {
        LOG_ERROR("[BTRuntime] Error loading BT: " << e.what());
        tree_loaded_ = false;
        return false;
    }
}

bool BTRuntime::start(int tick_rate_hz) {
    if (running_) {
        LOG_ERROR("[BTRuntime] Already running");
        return false;
    }

    if (!tree_loaded_) {
        LOG_ERROR("[BTRuntime] No BT loaded, call load_tree() first");
        return false;
    }

    if (tick_rate_hz <= 0 || tick_rate_hz > 1000) {
        LOG_ERROR("[BTRuntime] Invalid tick rate: " << tick_rate_hz << " (must be 1-1000 Hz)");
        return false;
    }

    tick_rate_hz_ = tick_rate_hz;
    running_ = true;

    tick_thread_ = std::make_unique<std::thread>(&BTRuntime::tick_loop, this);

    LOG_INFO("[BTRuntime] Started tick loop at " << tick_rate_hz << " Hz");
    return true;
}

void BTRuntime::stop() {
    if (!running_) {
        return;
    }

    LOG_INFO("[BTRuntime] Stopping tick loop...");
    running_ = false;

    if (tick_thread_ && tick_thread_->joinable()) {
        tick_thread_->join();
    }
    tick_thread_.reset();

    LOG_INFO("[BTRuntime] Tick loop stopped");
}

bool BTRuntime::is_running() const { return running_; }

BT::NodeStatus BTRuntime::tick() {
    if (!tree_) {
        LOG_ERROR("[BTRuntime] Cannot tick, no tree loaded");
        return BT::NodeStatus::FAILURE;
    }

    return tree_->tickOnce();
}

void BTRuntime::tick_loop() {
    using namespace std::chrono;

    const auto tick_period = milliseconds(1000 / tick_rate_hz_);
    auto next_tick = steady_clock::now() + tick_period;

    LOG_INFO("[BTRuntime] Tick loop started (period: " << tick_period.count() << "ms)");

    while (running_) {
        // Check if we're in AUTO mode
        if (mode_manager_.current_mode() != RuntimeMode::AUTO) {
            // Not in AUTO mode, skip tick
            std::this_thread::sleep_until(next_tick);
            next_tick += tick_period;
            continue;
        }

        // Populate blackboard with fresh StateCache snapshot
        populate_blackboard();

        // Execute single BT tick
        try {
            auto status = tick();

            // Log terminal states (optional, can be verbose)
            if (status == BT::NodeStatus::SUCCESS) {
                LOG_INFO("[BTRuntime] BT completed successfully");
            } else if (status == BT::NodeStatus::FAILURE) {
                LOG_WARN("[BTRuntime] BT failed");
            }
            // RUNNING status is normal, don't log
        } catch (const std::exception &e) {
            LOG_ERROR("[BTRuntime] Error during tick: " << e.what());
        }

        // Sleep until next tick
        std::this_thread::sleep_until(next_tick);
        next_tick += tick_period;
    }

    LOG_INFO("[BTRuntime] Tick loop exiting");
}
void BTRuntime::populate_blackboard() {
    if (!tree_) {
        return;
    }

    // Snapshot StateCache before tick
    // This ensures BT sees consistent state, no mid-tick changes visible
    //
    // Critical design notes:
    // - BT sees tick-consistent snapshot, NOT continuous state
    // - BT logic is edge-triggered by events, but state visibility is per-tick
    // - No mid-tick state changes visible to BT nodes
    // - BT is NOT for hard real-time control; call latency acceptable

    auto blackboard = tree_->rootBlackboard();

    // Store CallRouter reference for CallDeviceNode
    // BT nodes will cast this back to CallRouter* when needed
    blackboard->set("call_router", static_cast<void *>(&call_router_));
    // Store StateCache reference for ReadSignalNode
    // BT nodes will cast this back to StateCache* when needed
    blackboard->set("state_cache", static_cast<void *>(&state_cache_));

    // Store provider registry reference for CallDeviceNode
    // CallRouter::execute_call() requires provider registry
    blackboard->set("provider_registry", static_cast<void *>(&provider_registry_));

    // Add parameter_manager to blackboard
    if (parameter_manager_ != nullptr) {
        blackboard->set("parameter_manager", static_cast<void *>(parameter_manager_));
    }

    // Note: We pass references, not snapshots, for efficiency.
    // StateCache's get_signal_value() is already thread-safe.
    // This is acceptable because:
    // 1. Polling happens every 500ms, ticks every 100ms (10 Hz)
    // 2. BT execution is fast compared to poll rate
    // 3. If a value changes mid-tick, next tick will see the change
    // 4. BT is for orchestration policy, not hard real-time control
    // 5. Parameters are READ-ONLY from BT perspective - GetParameterNode queries ParameterManager
}

}  // namespace automation
}  // namespace anolis
