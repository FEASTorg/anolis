#include <gtest/gtest.h>

// Test critical dependencies and infrastructure
#include <atomic>
#include <chrono>
#include <nlohmann/json.hpp>
#include <thread>

#include "protocol.pb.h"

/**
 * @brief Infrastructure tests verify build system, dependencies, and basic features work.
 * These are not feature tests - they validate the foundation the codebase depends on.
 */

TEST(InfrastructureTest, ProtobufSerializationWorks) {
    // Verify protobuf can serialize and deserialize Value messages
    // This is critical since ADPP protocol depends on protobuf
    anolis::deviceprovider::v0::Value val;
    val.set_double_value(42.5);
    val.set_type(anolis::deviceprovider::v0::VALUE_TYPE_DOUBLE);

    // Serialize to string
    std::string serialized;
    ASSERT_TRUE(val.SerializeToString(&serialized));
    EXPECT_FALSE(serialized.empty());

    // Deserialize back
    anolis::deviceprovider::v0::Value deserialized;
    ASSERT_TRUE(deserialized.ParseFromString(serialized));

    // Verify values match
    EXPECT_DOUBLE_EQ(deserialized.double_value(), 42.5);
    EXPECT_EQ(deserialized.type(), anolis::deviceprovider::v0::VALUE_TYPE_DOUBLE);
}

TEST(InfrastructureTest, JsonParsingWorks) {
    // Verify nlohmann/json can parse and create JSON
    // Critical for HTTP API and config parsing
    const char* json_str = R"({"key":"value","number":42,"flag":true})";

    auto parsed = nlohmann::json::parse(json_str);
    EXPECT_EQ(parsed["key"], "value");
    EXPECT_EQ(parsed["number"], 42);
    EXPECT_TRUE(parsed["flag"]);

    // Verify JSON creation
    nlohmann::json created = {{"test", true}, {"num", 3.14}, {"str", "hello"}};
    EXPECT_TRUE(created["test"]);
    EXPECT_DOUBLE_EQ(created["num"], 3.14);
    EXPECT_EQ(created["str"], "hello");
}

TEST(InfrastructureTest, ThreadingAndAtomicsWork) {
    // Verify std::thread and std::atomic work correctly
    // Critical since runtime uses threads extensively (HTTP, polling, telemetry, automation)
    std::atomic<int> counter{0};
    std::atomic<bool> flag{false};

    std::thread t1([&counter]() {
        for (int i = 0; i < 1000; ++i) {
            counter.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::thread t2([&counter, &flag]() {
        for (int i = 0; i < 1000; ++i) {
            counter.fetch_add(1, std::memory_order_relaxed);
        }
        flag.store(true, std::memory_order_release);
    });

    t1.join();
    t2.join();

    EXPECT_EQ(counter.load(), 2000);
    EXPECT_TRUE(flag.load(std::memory_order_acquire));
}

TEST(InfrastructureTest, BasicLanguageFeaturesWork) {
    // Sanity check that compiler and standard library work as expected
    EXPECT_EQ(1 + 1, 2);
    EXPECT_NE(1.0, 2.0);
    EXPECT_TRUE(true);
    EXPECT_FALSE(false);

    // String operations
    std::string s = "hello";
    EXPECT_EQ(s.length(), 5);
    EXPECT_EQ(s + " world", "hello world");

    // Container operations
    std::vector<int> vec = {1, 2, 3};
    EXPECT_EQ(vec.size(), 3);
    EXPECT_EQ(vec[1], 2);
}
