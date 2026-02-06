#include "call_router.hpp"
#include "automation/mode_manager.hpp"
#include <iostream>
#include <sstream>

namespace anolis
{
    namespace control
    {

        CallRouter::CallRouter(const registry::DeviceRegistry &registry,
                               state::StateCache &state_cache)
            : registry_(registry), state_cache_(state_cache) {}

        void CallRouter::set_mode_manager(automation::ModeManager *mode_manager, const std::string &gating_policy)
        {
            mode_manager_ = mode_manager;
            manual_gating_policy_ = gating_policy;
        }

        CallResult CallRouter::execute_call(const CallRequest &request,
                                            std::unordered_map<std::string, std::shared_ptr<provider::IProviderHandle>> &providers)
        {
            CallResult result;
            result.success = false;

            // Phase 7B.2: Check manual/auto contention
            if (mode_manager_ && mode_manager_->current_mode() == automation::RuntimeMode::AUTO)
            {
                if (manual_gating_policy_ == "BLOCK")
                {
                    result.error_message = "Manual call blocked in AUTO mode (policy: BLOCK)";
                    result.status_code = anolis::deviceprovider::v0::Status_Code_CODE_FAILED_PRECONDITION;
                    std::cerr << "[CallRouter] WARNING: " << result.error_message << "\n";
                    return result;
                }
                else if (manual_gating_policy_ == "OVERRIDE")
                {
                    // Allow call to proceed
                }
            }

            // Validate call
            std::string validation_error;
            if (!validate_call(request, validation_error))
            {
                result.error_message = validation_error;
                if (validation_error.find("not found") != std::string::npos)
                    result.status_code = anolis::deviceprovider::v0::Status_Code_CODE_NOT_FOUND;
                else
                    result.status_code = anolis::deviceprovider::v0::Status_Code_CODE_INVALID_ARGUMENT;

                std::cerr << "[CallRouter] Validation failed: " << validation_error << "\n";
                return result;
            }

            // Parse device handle
            std::string provider_id, device_id;
            if (!parse_device_handle(request.device_handle, provider_id, device_id, result.error_message))
            {
                result.status_code = anolis::deviceprovider::v0::Status_Code_CODE_INVALID_ARGUMENT;
                return result;
            }

            // Get provider handle
            auto provider_it = providers.find(provider_id);
            if (provider_it == providers.end())
            {
                result.error_message = "Provider not found: " + provider_id;
                result.status_code = anolis::deviceprovider::v0::Status_Code_CODE_NOT_FOUND;
                std::cerr << "[CallRouter] ERROR: " << result.error_message << "\n";
                return result;
            }

            auto &provider = provider_it->second;
            if (!provider->is_available())
            {
                result.error_message = "Provider not available: " + provider_id;
                result.status_code = anolis::deviceprovider::v0::Status_Code_CODE_UNAVAILABLE;
                std::cerr << "[CallRouter] ERROR: " << result.error_message << "\n";
                return result;
            }

            // Get device and function spec for function_id
            const auto *device = registry_.get_device(provider_id, device_id);
            if (!device)
            {
                result.error_message = "Device not found in registry";
                result.status_code = anolis::deviceprovider::v0::Status_Code_CODE_NOT_FOUND;
                return result;
            }

            const registry::FunctionSpec *func_spec = nullptr;
            auto func_it = device->capabilities.functions_by_id.find(request.function_name);
            if (func_it == device->capabilities.functions_by_id.end())
            {
                result.error_message = "Function not found: " + request.function_name;
                result.status_code = anolis::deviceprovider::v0::Status_Code_CODE_NOT_FOUND;
                return result;
            }
            func_spec = &func_it->second;

            // Per-provider serialization (v0: simple mutex lock)
            std::lock_guard<std::mutex> lock(provider_locks_[provider_id]);

            // Forward call to provider
            anolis::deviceprovider::v0::CallResponse call_response;
            if (!provider->call(device_id, func_spec->function_id, request.function_name,
                                request.args, call_response))
            {
                result.error_message = "Provider call failed: " + provider->last_error();
                result.status_code = provider->last_status_code();
                std::cerr << "[CallRouter] Call failed: " << result.error_message << " (Code: " << result.status_code << ")\n";
                return result;
            }

            // Extract results
            for (const auto &[key, value] : call_response.results())
            {
                result.results[key] = value;
            }

            // Post-call state update: immediate poll of affected device
            state_cache_.poll_device_now(request.device_handle, providers);

            result.success = true;
            return result;
        }

        bool CallRouter::validate_call(const CallRequest &request, std::string &error) const
        {
            // Check device exists
            if (!validate_device_exists(request.device_handle, error))
            {
                return false;
            }

            // Parse device handle
            std::string provider_id, device_id;
            if (!parse_device_handle(request.device_handle, provider_id, device_id, error))
            {
                return false;
            }

            // Get device
            const auto *device = registry_.get_device(provider_id, device_id);
            if (!device)
            {
                error = "Device not found in registry: " + request.device_handle;
                return false;
            }

            // Check function exists
            const registry::FunctionSpec *func_spec = nullptr;
            if (!validate_function_exists(*device, request.function_name, func_spec, error))
            {
                return false;
            }

            // Validate arguments
            if (!validate_arguments(*func_spec, request.args, error))
            {
                return false;
            }

            return true;
        }

        bool CallRouter::validate_device_exists(const std::string &device_handle, std::string &error) const
        {
            const auto *device = registry_.get_device_by_handle(device_handle);
            if (!device)
            {
                error = "Device not found: " + device_handle;
                return false;
            }
            return true;
        }

        bool CallRouter::validate_function_exists(const registry::RegisteredDevice &device,
                                                  const std::string &function_name,
                                                  const registry::FunctionSpec *&out_spec,
                                                  std::string &error) const
        {
            auto it = device.capabilities.functions_by_id.find(function_name);
            if (it == device.capabilities.functions_by_id.end())
            {
                error = "Function not found: " + function_name + " on device " + device.get_handle();
                return false;
            }
            out_spec = &it->second;
            return true;
        }

        bool CallRouter::validate_arguments(const registry::FunctionSpec &spec,
                                            const std::map<std::string, anolis::deviceprovider::v0::Value> &args,
                                            std::string &error) const
        {
            // v0: Basic validation - check that all required params are present
            // Full type checking would require ArgSpec metadata (deferred to Phase 3B)

            // For now, just verify we have the expected number of arguments
            if (args.size() != spec.param_ids.size())
            {
                std::ostringstream oss;
                oss << "Argument count mismatch: expected " << spec.param_ids.size()
                    << ", got " << args.size();
                error = oss.str();
                return false;
            }

            // Check that all expected parameters are present
            for (const auto &param_id : spec.param_ids)
            {
                if (args.find(param_id) == args.end())
                {
                    error = "Missing required argument: " + param_id;
                    return false;
                }
            }

            return true;
        }

        bool CallRouter::parse_device_handle(const std::string &device_handle,
                                             std::string &provider_id,
                                             std::string &device_id,
                                             std::string &error) const
        {
            // Device handle format: "provider_id/device_id"
            auto slash_pos = device_handle.find('/');
            if (slash_pos == std::string::npos)
            {
                error = "Invalid device handle format (expected 'provider/device'): " + device_handle;
                return false;
            }

            provider_id = device_handle.substr(0, slash_pos);
            device_id = device_handle.substr(slash_pos + 1);

            if (provider_id.empty() || device_id.empty())
            {
                error = "Invalid device handle (empty provider or device): " + device_handle;
                return false;
            }

            return true;
        }

    } // namespace control
} // namespace anolis
