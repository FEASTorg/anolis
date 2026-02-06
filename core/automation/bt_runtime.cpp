#include "automation/bt_runtime.hpp"
#include "automation/bt_nodes.hpp"
#include "automation/mode_manager.hpp"
#include "automation/parameter_manager.hpp" // Phase 7C
#include "state/state_cache.hpp"
#include "control/call_router.hpp"

#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>

#include <behaviortree_cpp/bt_factory.h>
#include <behaviortree_cpp/blackboard.h>

namespace anolis
{
    namespace automation
    {

        BTRuntime::BTRuntime(state::StateCache &state_cache,
                             control::CallRouter &call_router,
                             std::unordered_map<std::string, std::shared_ptr<provider::IProviderHandle>> &providers,
                             ModeManager &mode_manager,
                             ParameterManager *parameter_manager)
            : state_cache_(state_cache), call_router_(call_router), providers_(providers), mode_manager_(mode_manager), parameter_manager_(parameter_manager), factory_(std::make_unique<BT::BehaviorTreeFactory>())
        {
            std::cout << "[BTRuntime] Initialized" << std::endl;

            // Phase 7A.3: Register custom nodes
            factory_->registerNodeType<ReadSignalNode>("ReadSignal");
            factory_->registerNodeType<CallDeviceNode>("CallDevice");
            factory_->registerNodeType<CheckQualityNode>("CheckQuality");
            factory_->registerNodeType<GetParameterNode>("GetParameter"); // Phase 7C

            std::cout << "[BTRuntime] Registered custom node types" << std::endl;
        }

        BTRuntime::~BTRuntime()
        {
            stop();
        }

        bool BTRuntime::load_tree(const std::string &path)
        {
            // Verify file exists
            std::ifstream file(path);
            if (!file.good())
            {
                std::cerr << "[BTRuntime] ERROR: Cannot open BT file: " << path << std::endl;
                return false;
            }

            tree_path_ = path;

            // MUST populate blackboard before creating tree!
            // BT nodes access blackboard during construction/initialization
            populate_blackboard();

            try
            {
                tree_ = std::make_unique<BT::Tree>(factory_->createTreeFromFile(path));
                tree_loaded_ = true;

                std::cout << "[BTRuntime] BT loaded successfully: " << path << std::endl;
                return true;
            }
            catch (const std::exception &e)
            {
                std::cerr << "[BTRuntime] ERROR loading BT: " << e.what() << std::endl;
                tree_loaded_ = false;
                return false;
            }
        }

        bool BTRuntime::start(int tick_rate_hz)
        {
            if (running_)
            {
                std::cerr << "[BTRuntime] ERROR: Already running" << std::endl;
                return false;
            }

            if (!tree_loaded_)
            {
                std::cerr << "[BTRuntime] ERROR: No BT loaded, call load_tree() first" << std::endl;
                return false;
            }

            if (tick_rate_hz <= 0 || tick_rate_hz > 1000)
            {
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

        void BTRuntime::stop()
        {
            if (!running_)
            {
                return;
            }

            std::cout << "[BTRuntime] Stopping tick loop..." << std::endl;
            running_ = false;

            if (tick_thread_ && tick_thread_->joinable())
            {
                tick_thread_->join();
            }
            tick_thread_.reset();

            std::cout << "[BTRuntime] Tick loop stopped" << std::endl;
        }

        bool BTRuntime::is_running() const
        {
            return running_;
        }

        BT::NodeStatus BTRuntime::tick()
        {
            if (!tree_)
            {
                std::cerr << "[BTRuntime] ERROR: Cannot tick, no tree loaded" << std::endl;
                return BT::NodeStatus::FAILURE;
            }

            return tree_->tickOnce();
        }

        void BTRuntime::tick_loop()
        {
            using namespace std::chrono;

            const auto tick_period = milliseconds(1000 / tick_rate_hz_);
            auto next_tick = steady_clock::now() + tick_period;

            std::cout << "[BTRuntime] Tick loop started (period: "
                      << tick_period.count() << "ms)" << std::endl;

            while (running_)
            {
                // Check if we're in AUTO mode (Phase 7B.3)
                if (mode_manager_.current_mode() != RuntimeMode::AUTO)
                {
                    // Not in AUTO mode, skip tick
                    std::this_thread::sleep_until(next_tick);
                    next_tick += tick_period;
                    continue;
                }

                // Populate blackboard with fresh StateCache snapshot
                populate_blackboard();

                // Execute single BT tick
                try
                {
                    auto status = tick();

                    // Log terminal states (optional, can be verbose)
                    if (status == BT::NodeStatus::SUCCESS)
                    {
                        std::cout << "[BTRuntime] BT completed successfully" << std::endl;
                    }
                    else if (status == BT::NodeStatus::FAILURE)
                    {
                        std::cout << "[BTRuntime] BT failed" << std::endl;
                    }
                    // RUNNING status is normal, don't log
                }
                catch (const std::exception &e)
                {
                    std::cerr << "[BTRuntime] ERROR during tick: " << e.what() << std::endl;
                }

                // Phase 7B: Check mode before next tick
                // if (mode_manager_->current_mode() != RuntimeMode::AUTO) {
                //     std::cout << "[BTRuntime] Not in AUTO mode, pausing tick" << std::endl;
                //     break;
                // }

                // Sleep until next tick
                std::this_thread::sleep_until(next_tick);
                next_tick += tick_period;
            }

            std::cout << "[BTRuntime] Tick loop exiting" << std::endl;
        }

        void BTRuntime::populate_blackboard()
        {
            if (!tree_)
            {
                return;
            }

            // Snapshot StateCache before tick
            // This ensures BT sees consistent state, no mid-tick changes visible
            //
            // Critical design notes (from Phase 7 review):
            // - BT sees tick-consistent snapshot, NOT continuous state
            // - BT logic is edge-triggered by events, but state visibility is per-tick
            // - No mid-tick state changes visible to BT nodes
            // - BT is NOT for hard real-time control; call latency acceptable

            auto blackboard = tree_->rootBlackboard();

            // Store CallRouter reference for CallDeviceNode (Phase 7A.3)
            // BT nodes will cast this back to CallRouter* when needed
            blackboard->set("call_router", static_cast<void *>(&call_router_));
            // Store StateCache reference for ReadSignalNode (Phase 7A.3)
            // BT nodes will cast this back to StateCache* when needed
            blackboard->set("state_cache", static_cast<void *>(&state_cache_));

            // Store providers map reference for CallDeviceNode (Phase 7A.5 fix)
            // CallRouter::execute_call() requires providers map
            blackboard->set("providers", static_cast<void *>(&providers_));

            // Phase 7C: Add parameter_manager to blackboard
            if (parameter_manager_)
            {
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

    } // namespace automation
} // namespace anolis
