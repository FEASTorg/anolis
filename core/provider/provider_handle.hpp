#pragma once

#include "provider_process.hpp"
#include "protocol.pb.h"
#include <string>
#include <vector>
#include <chrono>
#include <mutex>

namespace anolis
{
    namespace provider
    {

        // ProviderHandle provides high-level ADPP client API
        // Wraps ProviderProcess and handles protobuf serialization/deserialization
        class ProviderHandle
        {
        public:
            ProviderHandle(const std::string &provider_id, const std::string &executable_path,
                           const std::vector<std::string> &args = {});
            ~ProviderHandle() = default;

            // Delete copy/move
            ProviderHandle(const ProviderHandle &) = delete;
            ProviderHandle &operator=(const ProviderHandle &) = delete;

            // Start provider process and perform Hello handshake
            // Returns true on success, false on failure
            bool start();

            // Check if provider is available
            bool is_available() const { return process_.is_running(); }

            // ADPP Operations (all blocking, synchronous)
            bool hello(anolis::deviceprovider::v0::HelloResponse &response);
            bool list_devices(std::vector<anolis::deviceprovider::v0::Device> &devices);
            bool describe_device(const std::string &device_id,
                                 anolis::deviceprovider::v0::DescribeDeviceResponse &response);
            bool read_signals(const std::string &device_id,
                              const std::vector<std::string> &signal_ids,
                              anolis::deviceprovider::v0::ReadSignalsResponse &response);
            bool call(const std::string &device_id,
                      uint32_t function_id,
                      const std::string &function_name,
                      const std::map<std::string, anolis::deviceprovider::v0::Value> &args,
                      anolis::deviceprovider::v0::CallResponse &response);

            // Get last error message
            const std::string &last_error() const { return error_; }

            // Get last status code
            anolis::deviceprovider::v0::Status_Code last_status_code() const { return last_status_code_; }

            // Get provider ID
            const std::string &provider_id() const { return process_.provider_id(); }

        private:
            ProviderProcess process_;
            std::string error_;
            anolis::deviceprovider::v0::Status_Code last_status_code_ = anolis::deviceprovider::v0::Status_Code_CODE_OK;
            uint32_t next_request_id_;
            std::mutex mutex_;

            // Timeout for all operations (5 seconds)
            static constexpr int kTimeoutMs = 5000;

            // Send request and wait for response
            bool send_request(const anolis::deviceprovider::v0::Request &request,
                              anolis::deviceprovider::v0::Response &response);

            // Wait for response with timeout
            bool wait_for_response(anolis::deviceprovider::v0::Response &response, int timeout_ms);
        };

    } // namespace provider
} // namespace anolis
