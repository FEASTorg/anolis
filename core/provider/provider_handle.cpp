#include "provider_handle.hpp"
#include <iostream>
#include <thread>
#include <chrono>

namespace anolis
{
    namespace provider
    {

        ProviderHandle::ProviderHandle(const std::string &provider_id, const std::string &executable_path,
                                       const std::vector<std::string> &args)
            : process_(provider_id, executable_path, args), next_request_id_(1) {}

        bool ProviderHandle::start()
        {
            std::cerr << "[" << process_.provider_id() << "] Starting provider\n";

            // Spawn process
            if (!process_.spawn())
            {
                error_ = process_.last_error();
                return false;
            }

            // Perform Hello handshake
            anolis::deviceprovider::v0::HelloResponse hello_response;
            if (!hello(hello_response))
            {
                std::cerr << "[" << process_.provider_id() << "] Hello handshake failed: "
                          << error_ << "\n";
                return false;
            }

            std::cerr << "[" << process_.provider_id() << "] Hello succeeded: "
                      << hello_response.provider_name() << " v" << hello_response.provider_version() << "\n";
            return true;
        }

        bool ProviderHandle::hello(anolis::deviceprovider::v0::HelloResponse &response)
        {
            anolis::deviceprovider::v0::Request request;
            request.set_request_id(next_request_id_++);
            auto *hello_req = request.mutable_hello();
            hello_req->set_protocol_version("v0");
            hello_req->set_client_name("anolis-runtime");
            hello_req->set_client_version("0.1.0");

            anolis::deviceprovider::v0::Response resp;
            if (!send_request(request, resp))
            {
                return false;
            }

            if (!resp.has_hello())
            {
                error_ = "Response missing hello field";
                return false;
            }

            response = resp.hello();
            return true;
        }

        bool ProviderHandle::list_devices(std::vector<anolis::deviceprovider::v0::Device> &devices)
        {
            anolis::deviceprovider::v0::Request request;
            request.set_request_id(next_request_id_++);
            request.mutable_list_devices();

            anolis::deviceprovider::v0::Response resp;
            if (!send_request(request, resp))
            {
                return false;
            }

            if (!resp.has_list_devices())
            {
                error_ = "Response missing list_devices field";
                return false;
            }

            devices.clear();
            for (const auto &device : resp.list_devices().devices())
            {
                devices.push_back(device);
            }
            return true;
        }

        bool ProviderHandle::describe_device(const std::string &device_id,
                                             anolis::deviceprovider::v0::DescribeDeviceResponse &response)
        {
            anolis::deviceprovider::v0::Request request;
            request.set_request_id(next_request_id_++);
            request.mutable_describe_device()->set_device_id(device_id);

            anolis::deviceprovider::v0::Response resp;
            if (!send_request(request, resp))
            {
                return false;
            }

            if (!resp.has_describe_device())
            {
                error_ = "Response missing describe_device field";
                return false;
            }

            response = resp.describe_device();
            return true;
        }

        bool ProviderHandle::read_signals(const std::string &device_id,
                                          const std::vector<std::string> &signal_ids,
                                          anolis::deviceprovider::v0::ReadSignalsResponse &response)
        {
            anolis::deviceprovider::v0::Request request;
            request.set_request_id(next_request_id_++);
            auto *read_req = request.mutable_read_signals();
            read_req->set_device_id(device_id);
            for (const auto &sig_id : signal_ids)
            {
                read_req->add_signal_ids(sig_id);
            }

            anolis::deviceprovider::v0::Response resp;
            if (!send_request(request, resp))
            {
                return false;
            }

            if (!resp.has_read_signals())
            {
                error_ = "Response missing read_signals field";
                return false;
            }

            response = resp.read_signals();
            return true;
        }

        bool ProviderHandle::call(const std::string &device_id,
                                  uint32_t function_id,
                                  const std::string &function_name,
                                  const std::map<std::string, anolis::deviceprovider::v0::Value> &args,
                                  anolis::deviceprovider::v0::CallResponse &response)
        {
            anolis::deviceprovider::v0::Request request;
            request.set_request_id(next_request_id_++);
            auto *call_req = request.mutable_call();
            call_req->set_device_id(device_id);
            call_req->set_function_id(function_id);
            call_req->set_function_name(function_name);
            for (const auto &[key, value] : args)
            {
                (*call_req->mutable_args())[key] = value;
            }

            anolis::deviceprovider::v0::Response resp;
            if (!send_request(request, resp))
            {
                return false;
            }

            if (!resp.has_call())
            {
                error_ = "Response missing call field";
                return false;
            }

            response = resp.call();
            return true;
        }

        bool ProviderHandle::send_request(const anolis::deviceprovider::v0::Request &request,
                                          anolis::deviceprovider::v0::Response &response)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            // Check provider is running
            if (!process_.is_running())
            {
                error_ = "Provider process not running";
                return false;
            }

            // Serialize request
            std::string serialized;
            if (!request.SerializeToString(&serialized))
            {
                error_ = "Failed to serialize request";
                return false;
            }

            // Send frame
            if (!process_.client().write_frame(
                    reinterpret_cast<const uint8_t *>(serialized.data()),
                    serialized.size()))
            {
                error_ = "Failed to write request: " + process_.client().last_error();
                return false;
            }

            // Wait for response
            if (!wait_for_response(response, kTimeoutMs))
            {
                return false;
            }

            // Capture status code
            last_status_code_ = response.status().code();

            // Check status code
            if (last_status_code_ != anolis::deviceprovider::v0::Status_Code_CODE_OK)
            {
                error_ = "Provider returned error: " + response.status().message();
                return false;
            }

            return true;
        }

        bool ProviderHandle::wait_for_response(anolis::deviceprovider::v0::Response &response,
                                               int timeout_ms)
        {
            auto start = std::chrono::steady_clock::now();

            // Simple polling loop with timeout
            while (true)
            {
                // Check overall timeout
                auto elapsed = std::chrono::steady_clock::now() - start;
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
                if (elapsed_ms >= timeout_ms)
                {
                    error_ = "Timeout waiting for response (5s)";
                    std::cerr << "[" << process_.provider_id() << "] " << error_ << "\n";
                    return false;
                }

                int remaining = static_cast<int>(timeout_ms - elapsed_ms);
                // Poll in small chunks to check process status
                int poll_wait = (remaining > 50) ? 50 : remaining;

                if (process_.client().wait_for_data(poll_wait))
                {
                    // Data available, read full frame with TOTAL remaining time
                    elapsed = std::chrono::steady_clock::now() - start; // Update elapsed
                    elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
                    int read_timeout = static_cast<int>(timeout_ms - elapsed_ms);
                    if (read_timeout < 0) read_timeout = 0;

                    std::vector<uint8_t> frame_data;
                    if (process_.client().read_frame(frame_data, read_timeout))
                    {
                        // Parse response
                        if (!response.ParseFromArray(frame_data.data(), static_cast<int>(frame_data.size())))
                        {
                            error_ = "Failed to parse response protobuf";
                            return false;
                        }
                        return true;
                    }

                    if (!process_.client().last_error().empty())
                    {
                        error_ = "Failed to read response: " + process_.client().last_error();
                    } 
                    else
                    {
                         error_ = "Timed out reading response payload";
                    }
                    return false;
                }
                else if (!process_.client().last_error().empty())
                {
                    error_ = "Failed waiting for response: " + process_.client().last_error();
                    return false;
                }

                // Check if process died
                if (!process_.is_running())
                {
                    error_ = "Provider process died while waiting for response";
                    return false;
                }
            }
        }

    } // namespace provider
} // namespace anolis
