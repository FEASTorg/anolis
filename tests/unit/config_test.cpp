#include "runtime/config.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace anolis::runtime;

class ConfigTest : public ::testing::Test {
protected:
    fs::path temp_dir;

    void SetUp() override {
        // Create temporary test directory
        temp_dir = fs::temp_directory_path() / "anolis_config_test";
        fs::create_directories(temp_dir);
    }

    void TearDown() override {
        // Clean up temporary files
        if (fs::exists(temp_dir)) {
            fs::remove_all(temp_dir);
        }
    }

    std::string create_config_file(const std::string& name, const std::string& content) {
        fs::path config_path = temp_dir / name;
        std::ofstream file(config_path);
        file << content;
        file.close();
        return config_path.string();
    }
};

TEST_F(ConfigTest, ValidMinimalConfig) {
    std::string config_content = R"(
runtime:
  mode: MANUAL

http:
  enabled: true
  port: 8080

providers:
  - id: test
    command: /path/to/provider

logging:
  level: info
)";

    std::string config_path = create_config_file("minimal.yaml", config_content);
    RuntimeConfig config;
    std::string error;

    ASSERT_TRUE(load_config(config_path, config, error)) << "Error: " << error;
    EXPECT_EQ(config.http.port, 8080);
    EXPECT_EQ(config.providers.size(), 1);
    EXPECT_EQ(config.providers[0].id, "test");
}

TEST_F(ConfigTest, ValidModeMANUAL) {
    std::string config_content = R"(
runtime:
  mode: MANUAL

http:
  enabled: false

providers:
  - id: test
    command: /path/to/provider

logging:
  level: info
)";

    std::string config_path = create_config_file("mode_manual.yaml", config_content);
    RuntimeConfig config;
    std::string error;

    ASSERT_TRUE(load_config(config_path, config, error)) << "Error: " << error;
    // Mode validation happens in validate_config, not load_config
}

TEST_F(ConfigTest, ValidModeAUTO) {
    std::string config_content = R"(
runtime:
  mode: AUTO

http:
  enabled: false

providers:
  - id: test
    command: /path/to/provider

logging:
  level: info
)";

    std::string config_path = create_config_file("mode_auto.yaml", config_content);
    RuntimeConfig config;
    std::string error;

    ASSERT_TRUE(load_config(config_path, config, error)) << "Error: " << error;
}

TEST_F(ConfigTest, InvalidLogLevel) {
    std::string config_content = R"(
runtime:
  mode: MANUAL

http:
  enabled: false

providers:
  - id: test
    command: /path/to/provider

logging:
  level: INVALID_LEVEL
)";

    std::string config_path = create_config_file("invalid_log.yaml", config_content);
    RuntimeConfig config;
    std::string error;

    EXPECT_FALSE(load_config(config_path, config, error));
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("log level"), std::string::npos);
}

TEST_F(ConfigTest, NestedTelemetryStructure) {
    std::string config_content = R"(
runtime:
  mode: MANUAL

http:
  enabled: false

providers:
  - id: test
    command: /path/to/provider

logging:
  level: info

telemetry:
  enabled: true
  influxdb:
    url: http://localhost:8086
    org: testorg
    bucket: testbucket
    token: testtoken
    batch_size: 50
    flush_interval_ms: 500
)";

    std::string config_path = create_config_file("nested_telemetry.yaml", config_content);
    RuntimeConfig config;
    std::string error;

    ASSERT_TRUE(load_config(config_path, config, error)) << "Error: " << error;
    EXPECT_TRUE(config.telemetry.enabled);
    EXPECT_EQ(config.telemetry.influx_url, "http://localhost:8086");
    EXPECT_EQ(config.telemetry.influx_org, "testorg");
    EXPECT_EQ(config.telemetry.influx_bucket, "testbucket");
    EXPECT_EQ(config.telemetry.batch_size, 50);
    EXPECT_EQ(config.telemetry.flush_interval_ms, 500);
}

TEST_F(ConfigTest, UnknownKeysDoNotFailLoad) {
    // Unknown keys should generate warnings but not prevent loading
    std::string config_content = R"(
runtime:
  mode: MANUAL

http:
  enabled: false

providers:
  - id: test
    command: /path/to/provider

logging:
  level: info

unknown_top_level_key: some_value
another_unknown: 123
)";

    std::string config_path = create_config_file("unknown_keys.yaml", config_content);
    RuntimeConfig config;
    std::string error;

    // Should load despite unknown keys
    ASSERT_TRUE(load_config(config_path, config, error)) << "Error: " << error;
    EXPECT_EQ(config.providers.size(), 1);
}

TEST_F(ConfigTest, MissingProvidersSection) {
    std::string config_content = R"(
runtime:
  mode: MANUAL

http:
  enabled: false

logging:
  level: info
)";

    std::string config_path = create_config_file("no_providers.yaml", config_content);
    RuntimeConfig config;
    std::string error;

    // Should load but validation might fail
    bool loaded = load_config(config_path, config, error);
    if (!loaded) {
        // If load fails, it should mention providers
        EXPECT_NE(error.find("provider"), std::string::npos);
    } else {
        // If load succeeds, providers should be empty
        EXPECT_TRUE(config.providers.empty());
    }
}

TEST_F(ConfigTest, HTTPBindAddressConfiguration) {
    std::string config_content = R"(
runtime:
  mode: MANUAL

http:
  enabled: true
  bind: 0.0.0.0
  port: 9090

providers:
  - id: test
    command: /path/to/provider

logging:
  level: info
)";

    std::string config_path = create_config_file("http_bind.yaml", config_content);
    RuntimeConfig config;
    std::string error;

    ASSERT_TRUE(load_config(config_path, config, error)) << "Error: " << error;
    EXPECT_EQ(config.http.bind, "0.0.0.0");
    EXPECT_EQ(config.http.port, 9090);
}

TEST_F(ConfigTest, AutomationConfiguration) {
    std::string config_content = R"(
runtime:
  mode: MANUAL

http:
  enabled: false

providers:
  - id: test
    command: /path/to/provider

logging:
  level: info

automation:
  enabled: true
  behavior_tree: /path/to/tree.xml
  tick_rate_hz: 20
  manual_gating_policy: BLOCK
  parameters:
    - name: test_param
      type: double
      default: 10.5
      min: 0.0
      max: 100.0
)";

    std::string config_path = create_config_file("automation.yaml", config_content);
    RuntimeConfig config;
    std::string error;

    ASSERT_TRUE(load_config(config_path, config, error)) << "Error: " << error;
    EXPECT_TRUE(config.automation.enabled);
    EXPECT_EQ(config.automation.behavior_tree, "/path/to/tree.xml");
    EXPECT_EQ(config.automation.tick_rate_hz, 20);
    EXPECT_EQ(config.automation.parameters.size(), 1);
    EXPECT_EQ(config.automation.parameters[0].name, "test_param");
}

TEST_F(ConfigTest, ValidLogLevels) {
    const std::vector<std::string> valid_levels = {"debug", "info", "warn", "error"};

    for (const auto& level : valid_levels) {
        std::string config_content = R"(
runtime:
  mode: MANUAL

http:
  enabled: false

providers:
  - id: test
    command: /path/to/provider

logging:
  level: )" + level + R"(
)";

        std::string config_path = create_config_file("log_" + level + ".yaml", config_content);
        RuntimeConfig config;
        std::string error;

        EXPECT_TRUE(load_config(config_path, config, error)) << "Level: " << level << ", Error: " << error;
    }
}

TEST_F(ConfigTest, FileNotFound) {
    RuntimeConfig config;
    std::string error;

    EXPECT_FALSE(load_config("/nonexistent/path/config.yaml", config, error));
    EXPECT_FALSE(error.empty());
}
// ===== Restart Policy Tests =====

TEST_F(ConfigTest, RestartPolicyEnabled) {
    std::string config_content = R"(
runtime:
  mode: MANUAL

http:
  enabled: false

providers:
  - id: test_provider
    command: /path/to/provider
    restart_policy:
      enabled: true
      max_attempts: 3
      backoff_ms: [100, 1000, 5000]
      timeout_ms: 30000

logging:
  level: info
)";

    std::string config_path = create_config_file("restart_policy_enabled.yaml", config_content);
    RuntimeConfig config;
    std::string error;

    ASSERT_TRUE(load_config(config_path, config, error)) << "Error: " << error;
    ASSERT_EQ(config.providers.size(), 1);

    const auto& rp = config.providers[0].restart_policy;
    EXPECT_TRUE(rp.enabled);
    EXPECT_EQ(rp.max_attempts, 3);
    ASSERT_EQ(rp.backoff_ms.size(), 3);
    EXPECT_EQ(rp.backoff_ms[0], 100);
    EXPECT_EQ(rp.backoff_ms[1], 1000);
    EXPECT_EQ(rp.backoff_ms[2], 5000);
    EXPECT_EQ(rp.timeout_ms, 30000);
}

TEST_F(ConfigTest, RestartPolicyDefaults) {
    std::string config_content = R"(
runtime:
  mode: MANUAL

http:
  enabled: false

providers:
  - id: test_provider
    command: /path/to/provider

logging:
  level: info
)";

    std::string config_path = create_config_file("restart_policy_defaults.yaml", config_content);
    RuntimeConfig config;
    std::string error;

    ASSERT_TRUE(load_config(config_path, config, error)) << "Error: " << error;
    ASSERT_EQ(config.providers.size(), 1);

    const auto& rp = config.providers[0].restart_policy;
    EXPECT_FALSE(rp.enabled);  // Default disabled
    EXPECT_EQ(rp.max_attempts, 3);
    ASSERT_EQ(rp.backoff_ms.size(), 3);
    EXPECT_EQ(rp.timeout_ms, 30000);
}

TEST_F(ConfigTest, RestartPolicyBackoffMismatch) {
    std::string config_content = R"(
runtime:
  mode: MANUAL

http:
  enabled: false

providers:
  - id: test_provider
    command: /path/to/provider
    restart_policy:
      enabled: true
      max_attempts: 3
      backoff_ms: [100, 1000]
      timeout_ms: 30000

logging:
  level: info
)";

    std::string config_path = create_config_file("restart_policy_mismatch.yaml", config_content);
    RuntimeConfig config;
    std::string error;

    EXPECT_FALSE(load_config(config_path, config, error));
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("backoff_ms array length"), std::string::npos);
    EXPECT_NE(error.find("must match max_attempts"), std::string::npos);
}

TEST_F(ConfigTest, RestartPolicyNegativeBackoff) {
    std::string config_content = R"(
runtime:
  mode: MANUAL

http:
  enabled: false

providers:
  - id: test_provider
    command: /path/to/provider
    restart_policy:
      enabled: true
      max_attempts: 2
      backoff_ms: [100, -500]
      timeout_ms: 30000

logging:
  level: info
)";

    std::string config_path = create_config_file("restart_policy_negative.yaml", config_content);
    RuntimeConfig config;
    std::string error;

    EXPECT_FALSE(load_config(config_path, config, error));
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("backoff_ms"), std::string::npos);
    EXPECT_NE(error.find("must be >= 0"), std::string::npos);
}

TEST_F(ConfigTest, RestartPolicyInvalidMaxAttempts) {
    std::string config_content = R"(
runtime:
  mode: MANUAL

http:
  enabled: false

providers:
  - id: test_provider
    command: /path/to/provider
    restart_policy:
      enabled: true
      max_attempts: 0
      backoff_ms: []
      timeout_ms: 30000

logging:
  level: info
)";

    std::string config_path = create_config_file("restart_policy_invalid_attempts.yaml", config_content);
    RuntimeConfig config;
    std::string error;

    EXPECT_FALSE(load_config(config_path, config, error));
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("max_attempts must be >= 1"), std::string::npos);
}

TEST_F(ConfigTest, RestartPolicyShortTimeout) {
    std::string config_content = R"(
runtime:
  mode: MANUAL

http:
  enabled: false

providers:
  - id: test_provider
    command: /path/to/provider
    restart_policy:
      enabled: true
      max_attempts: 1
      backoff_ms: [100]
      timeout_ms: 500

logging:
  level: info
)";

    std::string config_path = create_config_file("restart_policy_short_timeout.yaml", config_content);
    RuntimeConfig config;
    std::string error;

    EXPECT_FALSE(load_config(config_path, config, error));
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("timeout_ms must be >= 1000ms"), std::string::npos);
}