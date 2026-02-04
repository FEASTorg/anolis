#include "automation/bt_runtime.hpp"
#include "state/state_cache.hpp"
#include "control/call_router.hpp"

#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>

// TODO: Uncomment when BehaviorTree.CPP integration is complete
// #include <behaviortree_cpp/bt_factory.h>
// #include <behaviortree_cpp/blackboard.h>

namespace anolis {
namespace automation {

BTRuntime::BTRuntime(state::StateCache& state_cache, control::CallRouter& call_router)
    : state_cache_(state_cache)
    , call_router_(call_router)
{
    std::cout << "[BTRuntime] Initialized (Phase 7A skeleton)" << std::endl;
}

BTRuntime::~BTRuntime() {
    stop();
}

bool BTRuntime::load_tree(const std::string& path) {
    // Verify file exists
    std::ifstream file(path);
    if (!file.good()) {
        std::cerr << "[BTRuntime] ERROR: Cannot open BT file: " << path << std::endl;
        return false;
    }

    tree_path_ = path;
    tree_loaded_ = false;  // Will be set to true when BehaviorTree.CPP integration is complete

    std::cout << "[BTRuntime] BT file located: " << path << std::endl;
    std::cout << "[BTRuntime] NOTE: BehaviorTree.CPP integration pending (Phase 7A.3+)" << std::endl;

    // TODO: Implement BT loading when BehaviorTree.CPP is integrated
    // BT::BehaviorTreeFactory factory;
    // 
    // // Register custom nodes (Phase 7A.3)
    // // factory.registerNodeType<ReadSignalNode>("ReadSignal");
    // // factory.registerNodeType<CallDeviceNode>("CallDevice");
    // // factory.registerNodeType<CheckQualityNode>("CheckQuality");
    // 
    // try {
    //     tree_ = factory.createTreeFromFile(path);
    //     tree_loaded_ = true;
    //     std::cout << "[BTRuntime] BT loaded successfully: " << path << std::endl;
    //     return true;
    // } catch (const std::exception& e) {
    //     std::cerr << "[BTRuntime] ERROR loading BT: " << e.what() << std::endl;
    //     return false;
    // }

    // Temporary: return true to allow skeleton testing
    tree_loaded_ = true;
    return true;
}

bool BTRuntime::start(int tick_rate_hz) {
    if (running_) {
        std::cerr << "[BTRuntime] ERROR: Already running" << std::endl;
        return false;
    }

    if (!tree_loaded_) {
        std::cerr << "[BTRuntime] ERROR: No BT loaded, call load_tree() first" << std::endl;
        return false;
    }

    if (tick_rate_hz <= 0 || tick_rate_hz > 1000) {
        std::cerr << "[BTRuntime] ERROR: Invalid tick rate: " << tick_rate_hz 
                  << " (must be 1-1000 Hz)" << std::endl;
        return false;
    }

    tick_rate_hz_ = tick_rate_hz;
    running_ = true;

    tick_thread_ = std::make_unique<std::thread>(&BTRuntime::tick_loop, this);

    std::cout << "[BTRuntime] Started tick loop at " << tick_rate_hz << " Hz" << std::endl;
    return true;
}

void BTRuntime::stop() {
    if (!running_) {
        return;
    }

    std::cout << "[BTRuntime] Stopping tick loop..." << std::endl;
    running_ = false;

    if (tick_thread_ && tick_thread_->joinable()) {
        tick_thread_->join();
    }
    tick_thread_.reset();

    std::cout << "[BTRuntime] Tick loop stopped" << std::endl;
}

bool BTRuntime::is_running() const {
    return running_;
}

void BTRuntime::tick_loop() {
    using namespace std::chrono;

    const auto tick_period = milliseconds(1000 / tick_rate_hz_);
    auto next_tick = steady_clock::now() + tick_period;

    std::cout << "[BTRuntime] Tick loop started (period: " 
              << tick_period.count() << "ms)" << std::endl;

    while (running_) {
        // Populate blackboard with fresh StateCache snapshot
        populate_blackboard();

        // Execute single BT tick
        // TODO: Uncomment when BehaviorTree.CPP is integrated
        // auto status = tree_->tickOnce();
        // 
        // // Log tick result (optional, can be verbose)
        // // std::cout << "[BTRuntime] Tick status: " << toStr(status) << std::endl;
        //
        // // Phase 7B: Check mode before next tick
        // // if (mode_manager_->current_mode() != RuntimeMode::AUTO) {
        // //     std::cout << "[BTRuntime] Not in AUTO mode, pausing tick" << std::endl;
        // //     break;
        // // }

        // Sleep until next tick
        std::this_thread::sleep_until(next_tick);
        next_tick += tick_period;
    }

    std::cout << "[BTRuntime] Tick loop exiting" << std::endl;
}

void BTRuntime::populate_blackboard() {
    // Snapshot StateCache before tick
    // This ensures BT sees consistent state, no mid-tick changes visible
    
    // TODO: Implement blackboard population (Phase 7A.2)
    // 
    // Critical design notes (from Phase 7 review):
    // - BT sees tick-consistent snapshot, NOT continuous state
    // - BT logic is edge-triggered by events, but state visibility is per-tick
    // - No mid-tick state changes visible to BT nodes
    // - BT is NOT for hard real-time control; call latency acceptable
    //
    // Blackboard schema (Phase 7A.2):
    // - StateCache snapshot: map<signal_id, {value, quality}>
    // - CallRouter reference (for device calls)
    // - Runtime parameters (Phase 7C placeholder)
    //
    // Example implementation:
    // auto blackboard = tree_->rootBlackboard();
    // 
    // // Get all signals from StateCache
    // auto signals = state_cache_.get_all_signals();
    // for (const auto& [signal_id, signal_data] : signals) {
    //     blackboard->set("signal_" + signal_id, signal_data.value);
    //     blackboard->set("quality_" + signal_id, signal_data.quality);
    // }
    // 
    // // Store CallRouter reference for CallDeviceNode
    // blackboard->set("call_router", &call_router_);
    //
    // // Phase 7C: Add parameters to blackboard
    // // blackboard->set("parameters", &parameter_manager_);
}

}  // namespace automation
}  // namespace anolis
