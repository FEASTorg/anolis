/**
 * mode_manager_test.cpp - ModeManager unit tests
 *
 * Tests:
 * - Valid state transitions (MANUAL↔AUTO, MANUAL↔IDLE, Any→FAULT, FAULT→MANUAL)
 * - Invalid transitions (FAULT→AUTO, FAULT→IDLE blocked)
 * - Mode change callbacks
 * - Thread-safety of mode queries
 * - String conversion functions
 * - Edge cases (same mode, multiple callbacks)
 */

#include "automation/mode_manager.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace anolis::automation;

/**
 * Test Fixture: ModeManagerTest
 *
 * Provides fresh ModeManager instance for each test.
 */
class ModeManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // ModeManager starts in MANUAL by default
        mode_manager_ = std::make_unique<ModeManager>();
    }

    void TearDown() override { mode_manager_.reset(); }

    std::unique_ptr<ModeManager> mode_manager_;
};

/******************************************************************************
 * String Conversion Tests
 ******************************************************************************/

TEST(ModeManagerStringTest, ModeToString) {
    EXPECT_STREQ(mode_to_string(RuntimeMode::MANUAL), "MANUAL");
    EXPECT_STREQ(mode_to_string(RuntimeMode::AUTO), "AUTO");
    EXPECT_STREQ(mode_to_string(RuntimeMode::IDLE), "IDLE");
    EXPECT_STREQ(mode_to_string(RuntimeMode::FAULT), "FAULT");
}

TEST(ModeManagerStringTest, StringToMode) {
    EXPECT_EQ(string_to_mode("MANUAL"), RuntimeMode::MANUAL);
    EXPECT_EQ(string_to_mode("AUTO"), RuntimeMode::AUTO);
    EXPECT_EQ(string_to_mode("IDLE"), RuntimeMode::IDLE);
    EXPECT_EQ(string_to_mode("FAULT"), RuntimeMode::FAULT);
}

TEST(ModeManagerStringTest, StringToModeInvalid) {
    // Invalid strings default to MANUAL
    EXPECT_EQ(string_to_mode("INVALID"), RuntimeMode::MANUAL);
    EXPECT_EQ(string_to_mode(""), RuntimeMode::MANUAL);
    EXPECT_EQ(string_to_mode("manual"), RuntimeMode::MANUAL);  // Case-sensitive
    EXPECT_EQ(string_to_mode("auto"), RuntimeMode::MANUAL);
}

TEST(ModeManagerStringTest, RoundTripConversion) {
    // Verify mode_to_string() -> string_to_mode() round-trip
    EXPECT_EQ(string_to_mode(mode_to_string(RuntimeMode::MANUAL)), RuntimeMode::MANUAL);
    EXPECT_EQ(string_to_mode(mode_to_string(RuntimeMode::AUTO)), RuntimeMode::AUTO);
    EXPECT_EQ(string_to_mode(mode_to_string(RuntimeMode::IDLE)), RuntimeMode::IDLE);
    EXPECT_EQ(string_to_mode(mode_to_string(RuntimeMode::FAULT)), RuntimeMode::FAULT);
}

/******************************************************************************
 * Initialization Tests
 ******************************************************************************/

TEST_F(ModeManagerTest, InitializesInManualByDefault) { EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::MANUAL); }

TEST(ModeManagerInitTest, InitializesInSpecifiedMode) {
    ModeManager manager_auto(RuntimeMode::AUTO);
    EXPECT_EQ(manager_auto.current_mode(), RuntimeMode::AUTO);

    ModeManager manager_idle(RuntimeMode::IDLE);
    EXPECT_EQ(manager_idle.current_mode(), RuntimeMode::IDLE);

    ModeManager manager_fault(RuntimeMode::FAULT);
    EXPECT_EQ(manager_fault.current_mode(), RuntimeMode::FAULT);
}

/******************************************************************************
 * Valid Transition Tests
 ******************************************************************************/

TEST_F(ModeManagerTest, TransitionManualToAuto) {
    std::string error;
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::AUTO, error));
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::AUTO);
    EXPECT_TRUE(error.empty());
}

TEST_F(ModeManagerTest, TransitionAutoToManual) {
    std::string error;
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::AUTO, error));
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::MANUAL, error));
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::MANUAL);
    EXPECT_TRUE(error.empty());
}

TEST_F(ModeManagerTest, TransitionManualToIdle) {
    std::string error;
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::IDLE, error));
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::IDLE);
    EXPECT_TRUE(error.empty());
}

TEST_F(ModeManagerTest, TransitionIdleToManual) {
    std::string error;
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::IDLE, error));
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::MANUAL, error));
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::MANUAL);
    EXPECT_TRUE(error.empty());
}

TEST_F(ModeManagerTest, TransitionToFaultFromAnyMode) {
    std::string error;

    // From MANUAL
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::FAULT, error));
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::FAULT);

    // From FAULT back to MANUAL
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::MANUAL, error));

    // From AUTO
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::AUTO, error));
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::FAULT, error));
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::FAULT);

    // From FAULT back to MANUAL
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::MANUAL, error));

    // From IDLE
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::IDLE, error));
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::FAULT, error));
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::FAULT);
}

TEST_F(ModeManagerTest, TransitionFaultToManualRecovery) {
    std::string error;

    // Enter FAULT
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::FAULT, error));
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::FAULT);

    // Recover to MANUAL
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::MANUAL, error));
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::MANUAL);
    EXPECT_TRUE(error.empty());
}

TEST_F(ModeManagerTest, SameModeIsNoOp) {
    std::string error;

    // Set to MANUAL (already in MANUAL)
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::MANUAL, error));
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::MANUAL);
    EXPECT_TRUE(error.empty());

    // Set to AUTO, then AUTO again
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::AUTO, error));
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::AUTO, error));
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::AUTO);
    EXPECT_TRUE(error.empty());
}

/******************************************************************************
 * Invalid Transition Tests (Safety-Critical)
 ******************************************************************************/

TEST_F(ModeManagerTest, BlockFaultToAutoTransition) {
    std::string error;

    // Enter FAULT
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::FAULT, error));

    // Attempt FAULT → AUTO (should fail)
    ASSERT_FALSE(mode_manager_->set_mode(RuntimeMode::AUTO, error));
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::FAULT);  // Still in FAULT
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("Invalid mode transition"), std::string::npos);
}

TEST_F(ModeManagerTest, BlockFaultToIdleTransition) {
    std::string error;

    // Enter FAULT
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::FAULT, error));

    // Attempt FAULT → IDLE (should fail)
    ASSERT_FALSE(mode_manager_->set_mode(RuntimeMode::IDLE, error));
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::FAULT);  // Still in FAULT
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("Invalid mode transition"), std::string::npos);
}

TEST_F(ModeManagerTest, BlockAutoToIdleDirectTransition) {
    std::string error;

    // Enter AUTO
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::AUTO, error));

    // Attempt AUTO → IDLE (should fail - must go through MANUAL)
    ASSERT_FALSE(mode_manager_->set_mode(RuntimeMode::IDLE, error));
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::AUTO);  // Still in AUTO
    EXPECT_FALSE(error.empty());
}

TEST_F(ModeManagerTest, BlockIdleToAutoDirectTransition) {
    std::string error;

    // Enter IDLE
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::IDLE, error));

    // Attempt IDLE → AUTO (should fail - must go through MANUAL)
    ASSERT_FALSE(mode_manager_->set_mode(RuntimeMode::AUTO, error));
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::IDLE);  // Still in IDLE
    EXPECT_FALSE(error.empty());
}

TEST_F(ModeManagerTest, FaultRecoveryRequiresManualStep) {
    std::string error;

    // Enter FAULT
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::FAULT, error));

    // Cannot go directly to AUTO
    ASSERT_FALSE(mode_manager_->set_mode(RuntimeMode::AUTO, error));

    // Must go through MANUAL first
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::MANUAL, error));
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::AUTO, error));
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::AUTO);
}

/******************************************************************************
 * Callback Tests
 ******************************************************************************/

TEST_F(ModeManagerTest, CallbackInvokedOnModeChange) {
    bool callback_invoked = false;
    RuntimeMode captured_old = RuntimeMode::MANUAL;
    RuntimeMode captured_new = RuntimeMode::MANUAL;

    mode_manager_->on_mode_change([&](RuntimeMode old_mode, RuntimeMode new_mode) {
        callback_invoked = true;
        captured_old = old_mode;
        captured_new = new_mode;
    });

    std::string error;
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::AUTO, error));

    EXPECT_TRUE(callback_invoked);
    EXPECT_EQ(captured_old, RuntimeMode::MANUAL);
    EXPECT_EQ(captured_new, RuntimeMode::AUTO);
}

TEST_F(ModeManagerTest, CallbackNotInvokedOnSameMode) {
    bool callback_invoked = false;

    mode_manager_->on_mode_change([&](RuntimeMode, RuntimeMode) { callback_invoked = true; });

    std::string error;
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::MANUAL, error));  // Same as current

    EXPECT_FALSE(callback_invoked);
}

TEST_F(ModeManagerTest, CallbackNotInvokedOnInvalidTransition) {
    bool callback_invoked = false;

    mode_manager_->on_mode_change([&](RuntimeMode, RuntimeMode) { callback_invoked = true; });

    std::string error;
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::FAULT, error));

    callback_invoked = false;  // Reset

    // Try invalid transition
    ASSERT_FALSE(mode_manager_->set_mode(RuntimeMode::AUTO, error));

    EXPECT_FALSE(callback_invoked);
}

TEST_F(ModeManagerTest, MultipleCallbacksInvoked) {
    int callback_count = 0;

    mode_manager_->on_mode_change([&](RuntimeMode, RuntimeMode) { callback_count++; });

    mode_manager_->on_mode_change([&](RuntimeMode, RuntimeMode) { callback_count++; });

    mode_manager_->on_mode_change([&](RuntimeMode, RuntimeMode) { callback_count++; });

    std::string error;
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::AUTO, error));

    EXPECT_EQ(callback_count, 3);
}

TEST_F(ModeManagerTest, CallbackExceptionsDoNotAbortTransition) {
    bool second_callback_invoked = false;

    // First callback throws
    mode_manager_->on_mode_change([](RuntimeMode, RuntimeMode) { throw std::runtime_error("Callback error"); });

    // Second callback should still be invoked
    mode_manager_->on_mode_change([&](RuntimeMode, RuntimeMode) { second_callback_invoked = true; });

    std::string error;
    ASSERT_TRUE(mode_manager_->set_mode(RuntimeMode::AUTO, error));

    // Mode change succeeded despite callback exception
    EXPECT_EQ(mode_manager_->current_mode(), RuntimeMode::AUTO);
    EXPECT_TRUE(second_callback_invoked);
}

/******************************************************************************
 * Thread-Safety Tests
 ******************************************************************************/

TEST_F(ModeManagerTest, ConcurrentModeQueriesAreSafe) {
    constexpr int kNumThreads = 10;
    constexpr int kQueriesPerThread = 1000;

    std::vector<std::thread> threads;
    std::atomic<int> query_count{0};

    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([&]() {
            for (int q = 0; q < kQueriesPerThread; ++q) {
                RuntimeMode mode = mode_manager_->current_mode();
                (void)mode;  // Suppress unused warning
                query_count++;
            }
        });
    }

    for (auto &t : threads) {
        t.join();
    }

    EXPECT_EQ(query_count.load(), kNumThreads * kQueriesPerThread);
}

TEST_F(ModeManagerTest, ConcurrentModeChangesAreSafe) {
    constexpr int kNumThreads = 8;
    constexpr int kChangesPerThread = 100;

    std::atomic<int> successful_transitions{0};
    std::atomic<int> failed_transitions{0};

    std::vector<std::thread> threads;

    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([&, i]() {
            for (int c = 0; c < kChangesPerThread; ++c) {
                std::string error;
                RuntimeMode target = (i % 2 == 0) ? RuntimeMode::AUTO : RuntimeMode::IDLE;

                // Try to transition
                if (mode_manager_->set_mode(target, error)) {
                    successful_transitions++;
                } else {
                    failed_transitions++;
                }

                // Return to MANUAL occasionally
                if (c % 10 == 0) {
                    mode_manager_->set_mode(RuntimeMode::MANUAL, error);
                }
            }
        });
    }

    for (auto &t : threads) {
        t.join();
    }

    // Mode should be in a valid state
    RuntimeMode final_mode = mode_manager_->current_mode();
    EXPECT_TRUE(final_mode == RuntimeMode::MANUAL || final_mode == RuntimeMode::AUTO ||
                final_mode == RuntimeMode::IDLE);

    // Total transitions attempted
    EXPECT_GT(successful_transitions.load(), 0);
}

TEST_F(ModeManagerTest, CallbackRegistrationDuringTransitionsIsSafe) {
    std::atomic<int> callback_count{0};
    std::atomic<bool> keep_running{true};

    // Thread 1: Register callbacks
    std::thread registration_thread([&]() {
        while (keep_running) {
            mode_manager_->on_mode_change([&](RuntimeMode, RuntimeMode) { callback_count++; });
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });

    // Thread 2: Trigger mode changes
    std::thread transition_thread([&]() {
        for (int i = 0; i < 50; ++i) {
            std::string error;
            mode_manager_->set_mode(RuntimeMode::AUTO, error);
            mode_manager_->set_mode(RuntimeMode::MANUAL, error);
            std::this_thread::sleep_for(std::chrono::microseconds(20));
        }
    });

    transition_thread.join();
    keep_running = false;
    registration_thread.join();

    // No crashes means thread-safety is working
    EXPECT_GT(callback_count.load(), 0);
}
