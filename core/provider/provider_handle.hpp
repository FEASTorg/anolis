#pragma once

#include "provider_process.hpp"
#include "i_provider_handle.hpp"
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
        class ProviderHandle : public IProviderHandle
        {
        public:
            ProviderHandle(const std::string &provider_id, const std::string &executable_path,
                           const std::vector<std::string> &args = {}, int timeout_ms = 5000);
            ~ProviderHandle() override = default;

            // Delete copy/move
            ProviderHandle(const ProviderHandle &) = delete;
            ProviderHandle &operator=(const ProviderHandle &) = delete;

            // Start provider process and perform Hello handshake
            // Returns true on success, false on failure
            bool start() override;

            // Check if provider is available
            bool is_available() const override { return process_.is_running(); }

            // ADPP Operations (all blocking, synchronous)
            bool hello(anolis::deviceprovider::v0::HelloResponse &response) override;
            bool list_devices(std::vector<anolis::deviceprovider::v0::Device> &devices) override;
            bool describe_device(const std::string &device_id,
                                 anolis::deviceprovider::v0::DescribeDeviceResponse &response) override;
            bool read_signals(const std::string &device_id,
                              const std::vector<std::string> &signal_ids,
                              anolis::deviceprovider::v0::ReadSignalsResponse &response) override;
            bool call(const std::string &device_id,
                      uint32_t function_id,
                      const std::string &function_name,
                      const std::map<std::string, anolis::deviceprovider::v0::Value> &args,
                      anolis::deviceprovider::v0::CallResponse &response) override;

            // Get last error message
            const std::string &last_error() const override { return error_; }

            // Get last status code
            anolis::deviceprovider::v0::Status_Code last_status_code() const override { return last_status_code_; }

            // Get provider ID
            const std::string &provider_id() const override { return process_.provider_id(); }

        private:
            ProviderProcess process_;
            std::string error_;
            anolis::deviceprovider::v0::Status_Code last_status_code_ = anolis::deviceprovider::v0::Status_Code_CODE_OK;
            uint32_t next_request_id_;
            std::mutex mutex_;

            // Timeout for operations
            int timeout_ms_;

            // Send request and wait for response
            bool send_request(const anolis::deviceprovider::v0::Request &request,
                              anolis::deviceprovider::v0::Response &response);

            // Wait for response with timeout
            bool wait_for_response(anolis::deviceprovider::v0::Response &response, int timeout_ms);
        };

    } // namespace provider
} // namespace anolis
