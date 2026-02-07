#include "state/state_cache.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "mocks/mock_provider_handle.hpp"
#include "provider/i_provider_handle.hpp"
#include "registry/device_registry.hpp"

using namespace anolis;
using namespace testing;
using namespace anolis::tests;

class StateCacheTest : public Test {
protected:
    void SetUp() override {
        registry = std::make_unique<registry::DeviceRegistry>();
        mock_provider = std::make_shared<StrictMock<MockProviderHandle>>();
        EXPECT_CALL(*mock_provider, provider_id()).WillRepeatedly(ReturnRef(mock_provider->_id));
        EXPECT_CALL(*mock_provider, is_available()).WillRepeatedly(Return(true));
        providers["sim0"] = mock_provider;
    }

    void RegisterMockDevice() {
        // Mock list_devices
        EXPECT_CALL(*mock_provider, list_devices(_)).WillOnce(Invoke([](std::vector<Device> &devices) {
            Device dev;
            dev.set_device_id("dev1");
            dev.set_label("Test Device");
            devices.push_back(dev);
            return true;
        }));

        // Mock describe_device
        EXPECT_CALL(*mock_provider, describe_device("dev1", _))
            .WillOnce(Invoke([](const std::string &, DescribeDeviceResponse &response) {
                auto *device = response.mutable_device();
                device->set_device_id("dev1");
                device->set_label("Test Device");

                // Add a signal
                auto *caps = response.mutable_capabilities();
                auto *sig = caps->add_signals();
                sig->set_signal_id("temp");
                sig->set_value_type(anolis::deviceprovider::v0::VALUE_TYPE_DOUBLE);
                sig->set_poll_hint_hz(1.0);  // Implies default
                return true;
            }));

        registry->discover_provider("sim0", *mock_provider);
    }

    std::unique_ptr<registry::DeviceRegistry> registry;
    std::shared_ptr<MockProviderHandle> mock_provider;
    std::unique_ptr<state::StateCache> state_cache;
    std::unordered_map<std::string, std::shared_ptr<provider::IProviderHandle>> providers;
};

TEST_F(StateCacheTest, Initialization) {
    state_cache = std::make_unique<state::StateCache>(*registry, 100);
    EXPECT_TRUE(state_cache->initialize());
}

TEST_F(StateCacheTest, PollAndRead) {
    RegisterMockDevice();

    state_cache = std::make_unique<state::StateCache>(*registry, 100);
    EXPECT_TRUE(state_cache->initialize());

    // Expect polls
    EXPECT_CALL(*mock_provider, read_signals("dev1", _, _))
        .WillOnce(Invoke([](const std::string &, const std::vector<std::string> &ids, ReadSignalsResponse &response) {
            // Verify we are asked for "temp"
            bool asking_temp = false;
            for (const auto &id : ids)
                if (id == "temp") asking_temp = true;
            if (!asking_temp) {
                return false;
            }

            auto *v = response.add_values();
            v->set_signal_id("temp");
            v->mutable_value()->set_double_value(25.5);
            v->set_quality(anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_OK);
            return true;
        }));

    state_cache->poll_once(providers);

    auto result = state_cache->get_signal_value("sim0/dev1", "temp");
    ASSERT_TRUE(result != nullptr);
    EXPECT_DOUBLE_EQ(result->value.double_value(), 25.5);
    EXPECT_FALSE(result->is_stale(std::chrono::seconds(2)));
}

TEST_F(StateCacheTest, Staleness) {
    RegisterMockDevice();
    state_cache = std::make_unique<state::StateCache>(*registry, 100);
    EXPECT_TRUE(state_cache->initialize());

    // 1. Initial State: Stale Quality
    EXPECT_CALL(*mock_provider, read_signals("dev1", _, _))
        .WillOnce(Invoke([](const std::string &, const std::vector<std::string> &, ReadSignalsResponse &response) {
            auto *v = response.add_values();
            v->set_signal_id("temp");
            v->mutable_value()->set_double_value(25.5);
            v->set_quality(anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_STALE);
            return true;
        }));

    state_cache->poll_once(providers);
    auto result = state_cache->get_signal_value("sim0/dev1", "temp");
    ASSERT_TRUE(result != nullptr);
    EXPECT_TRUE(result->is_stale(std::chrono::seconds(2)));
}

TEST_F(StateCacheTest, TimeBasedStaleness) {
    RegisterMockDevice();
    state_cache = std::make_unique<state::StateCache>(*registry, 100);
    EXPECT_TRUE(state_cache->initialize());

    // 1. Poll with OK quality
    EXPECT_CALL(*mock_provider, read_signals("dev1", _, _))
        .WillOnce(Invoke([](const std::string &, const std::vector<std::string> &, ReadSignalsResponse &response) {
            auto *v = response.add_values();
            v->set_signal_id("temp");
            v->mutable_value()->set_double_value(25.5);
            v->set_quality(anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_OK);
            return true;
        }));

    // Perform poll
    state_cache->poll_once(providers);

    // Capture the time "now" effectively used by the poll (it uses system_clock internally for setting timestamp during
    // poll) Actually, poll_once sets the timestamp = now(). We can't easily inject time into poll_once without
    // refactoring StateCache::poll_once too, BUT we can use the time we pass to is_stale to simulate the passage of
    // time relative to whenever the poll happened.

    auto result = state_cache->get_signal_value("sim0/dev1", "temp");
    ASSERT_TRUE(result != nullptr);

    // Get the timestamp from the result (it's public in the struct usually, or accessible)
    // Wait, CachedSignalValue struct definition in header:
    // struct CachedSignalValue { ... time_point timestamp; ... }
    // It's a struct, so public by default.

    auto poll_time = result->timestamp;

    // Check at poll time -> Not stale
    EXPECT_FALSE(result->is_stale(std::chrono::seconds(2), poll_time));
    EXPECT_EQ(result->age(poll_time).count(), 0);

    // Check 1 second later -> Not stale (limit is 2000ms)
    EXPECT_FALSE(result->is_stale(std::chrono::seconds(2), poll_time + std::chrono::seconds(1)));

    // Check 3 seconds later -> Stale
    EXPECT_TRUE(result->is_stale(std::chrono::seconds(2), poll_time + std::chrono::seconds(3)));
    EXPECT_GE(result->age(poll_time + std::chrono::seconds(3)).count(), 3000);
}

TEST_F(StateCacheTest, ConcurrencyStress) {
    RegisterMockDevice();
    state_cache = std::make_unique<state::StateCache>(*registry, 10);  // Fast poll
    EXPECT_TRUE(state_cache->initialize());

    std::atomic<bool> running{true};
    std::atomic<bool> failed{false};

    // Writer Thread (Simulated Poll)
    std::thread writer([&]() {
        // Setup a persistent expectation that returns changing values
        EXPECT_CALL(*mock_provider, read_signals("dev1", _, _))
            .WillRepeatedly(
                Invoke([](const std::string &, const std::vector<std::string> &, ReadSignalsResponse &response) {
                    auto *v = response.add_values();
                    v->set_signal_id("temp");
                    v->mutable_value()->set_double_value(rand() % 100);
                    v->set_quality(anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_OK);
                    return true;
                }));

        while (running) {
            state_cache->poll_once(providers);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    // Reader Threads
    std::vector<std::thread> readers;
    readers.reserve(5);
    for (int i = 0; i < 5; ++i) {
        readers.emplace_back([&]() {
            while (running) {
                try {
                    auto res = state_cache->get_signal_value("sim0/dev1", "temp");
                    // Just access it to ensure no segfault
                    if (res && res->value.double_value() < -1) failed = true;
                } catch (...) {
                    failed = true;
                }
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    running = false;

    writer.join();
    for (auto &t : readers) t.join();

    EXPECT_FALSE(failed);
}
