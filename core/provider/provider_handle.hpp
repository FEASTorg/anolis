#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "i_provider_handle.hpp"
#include "protocol.pb.h"
#include "provider_process.hpp"

namespace anolis {
namespace provider {

// ProviderHandle provides high-level ADPP client API
// Wraps ProviderProcess and handles protobuf serialization/deserialization
class ProviderHandle : public IProviderHandle {
public:
    ProviderHandle(const std::string &provider_id, const std::string &executable_path,
                   const std::vector<std::string> &args = {}, int timeout_ms = 5000, int hello_timeout_ms = 5000,
                   int ready_timeout_ms = 60000, int shutdown_timeout_ms = 2000);
    ~ProviderHandle() override = default;

    // Delete copy/move
    ProviderHandle(const ProviderHandle &) = delete;
    ProviderHandle &operator=(const ProviderHandle &) = delete;

    // Start provider process and perform Hello handshake
    // Returns true on success, false on failure
    bool start() override;

    // Check if provider is available
    bool is_available() const override {
        return session_healthy_.load(std::memory_order_acquire) && process_.is_running();
    }

    // ADPP Operations (all blocking, synchronous)
    bool hello(anolis::deviceprovider::v1::HelloResponse &response) override;
    bool wait_ready(anolis::deviceprovider::v1::WaitReadyResponse &response);
    bool list_devices(std::vector<anolis::deviceprovider::v1::Device> &devices) override;
    bool describe_device(const std::string &device_id,
                         anolis::deviceprovider::v1::DescribeDeviceResponse &response) override;
    bool read_signals(const std::string &device_id, const std::vector<std::string> &signal_ids,
                      anolis::deviceprovider::v1::ReadSignalsResponse &response) override;
    bool call(const std::string &device_id, uint32_t function_id, const std::string &function_name,
              const std::map<std::string, anolis::deviceprovider::v1::Value> &args,
              anolis::deviceprovider::v1::CallResponse &response) override;

    // Get last error message
    const std::string &last_error() const override { return error_; }

    // Get last status code
    anolis::deviceprovider::v1::Status_Code last_status_code() const override { return last_status_code_; }

    // Get provider ID
    const std::string &provider_id() const override { return process_.provider_id(); }

private:
    ProviderProcess process_;
    std::atomic<bool> session_healthy_{false};
    std::string error_;
    anolis::deviceprovider::v1::Status_Code last_status_code_ = anolis::deviceprovider::v1::Status_Code_CODE_OK;
    std::atomic<uint32_t> next_request_id_;
    std::mutex mutex_;

    // Timeout for operations
    int timeout_ms_;        // ADPP operation timeout
    int hello_timeout_ms_;  // Process liveness check timeout
    int ready_timeout_ms_;  // Hardware initialization timeout

    // Send request and wait for response
    bool send_request(const anolis::deviceprovider::v1::Request &request,
                      anolis::deviceprovider::v1::Response &response, uint64_t request_id);

    // Wait for response with timeout and request_id validation
    bool wait_for_response(anolis::deviceprovider::v1::Response &response, uint64_t expected_request_id,
                           int timeout_ms);
};

}  // namespace provider
}  // namespace anolis
