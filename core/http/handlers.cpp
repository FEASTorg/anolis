// Prevent Windows macro pollution (must be first)
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "server.hpp"
#include "errors.hpp"
#include "json.hpp"
#include "registry/device_registry.hpp"
#include "state/state_cache.hpp"
#include "control/call_router.hpp"
#include "events/event_emitter.hpp"
#include "automation/mode_manager.hpp"
#include "automation/parameter_manager.hpp" // Phase 7C
#include <chrono>

namespace anolis
{
    namespace http
    {

        // Helper: Parse provider_id and device_id from regex matches
        static bool parse_path_params(const httplib::Request &req,
                                      std::string &provider_id,
                                      std::string &device_id)
        {
            if (req.matches.size() >= 3)
            {
                provider_id = req.matches[1].str();
                device_id = req.matches[2].str();
                return true;
            }
            return false;
        }

        // Helper: Send JSON response
        static void send_json(httplib::Response &res, StatusCode code, const nlohmann::json &body)
        {
            res.status = status_code_to_http(code);
            res.set_content(body.dump(), "application/json");
        }

        //=============================================================================
        // GET /v0/devices
        //=============================================================================
        void HttpServer::handle_get_devices(const httplib::Request &req, httplib::Response &res)
        {
            auto devices = registry_.get_all_devices();

            nlohmann::json devices_json = nlohmann::json::array();
            for (const auto *device : devices)
            {
                devices_json.push_back(encode_device_info(*device));
            }

            nlohmann::json response = {
                {"status", make_status(StatusCode::OK)},
                {"devices", devices_json}};

            send_json(res, StatusCode::OK, response);
        }

        //=============================================================================
        // GET /v0/devices/{provider_id}/{device_id}/capabilities
        //=============================================================================
        void HttpServer::handle_get_device_capabilities(const httplib::Request &req, httplib::Response &res)
        {
            std::string provider_id, device_id;
            if (!parse_path_params(req, provider_id, device_id))
            {
                send_json(res, StatusCode::INVALID_ARGUMENT,
                          make_error_response(StatusCode::INVALID_ARGUMENT, "Invalid path parameters"));
                return;
            }

            const auto *device = registry_.get_device(provider_id, device_id);
            if (!device)
            {
                send_json(res, StatusCode::NOT_FOUND,
                          make_error_response(StatusCode::NOT_FOUND,
                                              "Device not found: " + provider_id + "/" + device_id));
                return;
            }

            nlohmann::json response = {
                {"status", make_status(StatusCode::OK)},
                {"provider_id", provider_id},
                {"device_id", device_id},
                {"capabilities", encode_capabilities(device->capabilities)}};

            send_json(res, StatusCode::OK, response);
        }

        //=============================================================================
        // GET /v0/state
        //=============================================================================
        void HttpServer::handle_get_state(const httplib::Request &req, httplib::Response &res)
        {
            auto now = std::chrono::system_clock::now();
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now.time_since_epoch())
                              .count();

            auto devices = registry_.get_all_devices();
            nlohmann::json devices_json = nlohmann::json::array();

            for (const auto *device : devices)
            {
                auto state = state_cache_.get_device_state(device->get_handle());
                if (state)
                {
                    devices_json.push_back(
                        encode_device_state(*state, device->provider_id, device->device_id));
                }
            }

            nlohmann::json response = {
                {"status", make_status(StatusCode::OK)},
                {"generated_at_epoch_ms", now_ms},
                {"devices", devices_json}};

            send_json(res, StatusCode::OK, response);
        }

        //=============================================================================
        // GET /v0/state/{provider_id}/{device_id}
        //=============================================================================
        void HttpServer::handle_get_device_state(const httplib::Request &req, httplib::Response &res)
        {
            std::string provider_id, device_id;
            if (!parse_path_params(req, provider_id, device_id))
            {
                send_json(res, StatusCode::INVALID_ARGUMENT,
                          make_error_response(StatusCode::INVALID_ARGUMENT, "Invalid path parameters"));
                return;
            }

            const auto *device = registry_.get_device(provider_id, device_id);
            if (!device)
            {
                send_json(res, StatusCode::NOT_FOUND,
                          make_error_response(StatusCode::NOT_FOUND,
                                              "Device not found: " + provider_id + "/" + device_id));
                return;
            }

            auto state = state_cache_.get_device_state(device->get_handle());
            if (!state)
            {
                // Device exists but no state yet (shouldn't normally happen)
                send_json(res, StatusCode::UNAVAILABLE,
                          make_error_response(StatusCode::UNAVAILABLE, "Device state not available"));
                return;
            }

            // Handle optional signal_id query params
            std::vector<std::string> filter_signals;
            for (size_t i = 0; req.has_param("signal_id"); i++)
            {
                // Collect all signal_id params
                auto it = req.params.find("signal_id");
                if (it != req.params.end())
                {
                    // Get all values for signal_id
                    auto range = req.params.equal_range("signal_id");
                    for (auto it2 = range.first; it2 != range.second; ++it2)
                    {
                        filter_signals.push_back(it2->second);
                    }
                    break;
                }
            }

            auto now = std::chrono::system_clock::now();
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now.time_since_epoch())
                              .count();

            // Build filtered or full state
            nlohmann::json values = nlohmann::json::array();

            auto worst_quality = anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_OK;

            for (const auto &[signal_id, cached] : state->signals)
            {
                // Apply filter if specified
                if (!filter_signals.empty())
                {
                    bool found = false;
                    for (const auto &fs : filter_signals)
                    {
                        if (fs == signal_id)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        continue;
                }

                values.push_back(encode_signal_value(cached, signal_id));
                if (cached.quality > worst_quality)
                {
                    worst_quality = cached.quality;
                }
            }

            std::string device_quality = state->provider_available ? quality_to_string(worst_quality) : "UNAVAILABLE";

            nlohmann::json response = {
                {"status", make_status(StatusCode::OK)},
                {"generated_at_epoch_ms", now_ms},
                {"provider_id", provider_id},
                {"device_id", device_id},
                {"quality", device_quality},
                {"values", values}};

            send_json(res, StatusCode::OK, response);
        }

        //=============================================================================
        // POST /v0/call
        //=============================================================================
        void HttpServer::handle_post_call(const httplib::Request &req, httplib::Response &res)
        {
            try
            {
                // Parse request body
                nlohmann::json request_json;
                try
                {
                    request_json = nlohmann::json::parse(req.body);
                }
                catch (const std::exception &e)
                {
                    send_json(res, StatusCode::INVALID_ARGUMENT,
                              make_error_response(StatusCode::INVALID_ARGUMENT,
                                                  std::string("Invalid JSON: ") + e.what()));
                    return;
                }

                // Decode call request
                std::string provider_id, device_id;
                uint32_t function_id;
                std::map<std::string, anolis::deviceprovider::v0::Value> args;
                std::string error;

                if (!decode_call_request(request_json, provider_id, device_id, function_id, args, error))
                {
                    send_json(res, StatusCode::INVALID_ARGUMENT,
                              make_error_response(StatusCode::INVALID_ARGUMENT, error));
                    return;
                }

                // Check if provider exists
                auto provider_it = providers_.find(provider_id);
                if (provider_it == providers_.end())
                {
                    send_json(res, StatusCode::NOT_FOUND,
                              make_error_response(StatusCode::NOT_FOUND,
                                                  "Provider not found: " + provider_id));
                    return;
                }

                // Check if provider is available
                if (!provider_it->second->is_available())
                {
                    send_json(res, StatusCode::UNAVAILABLE,
                              make_error_response(StatusCode::UNAVAILABLE,
                                                  "Provider unavailable: " + provider_id));
                    return;
                }

                // Look up device to get function name
                const auto *device = registry_.get_device(provider_id, device_id);
                if (!device)
                {
                    send_json(res, StatusCode::NOT_FOUND,
                              make_error_response(StatusCode::NOT_FOUND,
                                                  "Device not found: " + provider_id + "/" + device_id));
                    return;
                }

                // Find function by ID
                std::string function_name;
                for (const auto &[fname, spec] : device->capabilities.functions_by_id)
                {
                    if (spec.function_id == function_id)
                    {
                        function_name = fname;
                        break;
                    }
                }

                if (function_name.empty())
                {
                    send_json(res, StatusCode::NOT_FOUND,
                              make_error_response(StatusCode::NOT_FOUND,
                                                  "Function ID not found: " + std::to_string(function_id)));
                    return;
                }

                // Build CallRequest
                control::CallRequest call_request;
                call_request.device_handle = device->get_handle();
                call_request.function_name = function_name;
                call_request.args = args;

                // Execute call
                auto result = call_router_.execute_call(call_request, providers_);

                if (!result.success)
                {
                    // Determine error type
                    StatusCode status = StatusCode::INTERNAL;
                    if (result.error_message.find("not found") != std::string::npos ||
                        result.error_message.find("does not exist") != std::string::npos)
                    {
                        status = StatusCode::NOT_FOUND;
                    }
                    else if (result.error_message.find("argument") != std::string::npos ||
                             result.error_message.find("parameter") != std::string::npos ||
                             result.error_message.find("validation") != std::string::npos)
                    {
                        status = StatusCode::INVALID_ARGUMENT;
                    }
                    else if (result.error_message.find("timeout") != std::string::npos ||
                             result.error_message.find("deadline") != std::string::npos)
                    {
                        status = StatusCode::DEADLINE_EXCEEDED;
                    }
                    else if (result.error_message.find("unavailable") != std::string::npos)
                    {
                        status = StatusCode::UNAVAILABLE;
                    }

                    std::cerr << "[HTTP] Call failed: " << result.error_message << ", returning " << static_cast<int>(status) << "\n"
                              << std::flush;
                    send_json(res, status, make_error_response(status, result.error_message));
                    return;
                }

                nlohmann::json response = {
                    {"status", make_status(StatusCode::OK)},
                    {"provider_id", provider_id},
                    {"device_id", device_id},
                    {"function_id", function_id},
                    {"post_call_poll_triggered", true}};

                send_json(res, StatusCode::OK, response);
            }
            catch (const std::exception &e)
            {
                std::cerr << "[HTTP] Exception in handle_post_call: " << e.what() << "\n"
                          << std::flush;
                send_json(res, StatusCode::INTERNAL,
                          make_error_response(StatusCode::INTERNAL, std::string("Exception: ") + e.what()));
            }
        }

        //=============================================================================
        // GET /v0/runtime/status
        //=============================================================================
        void HttpServer::handle_get_runtime_status(const httplib::Request &req, httplib::Response &res)
        {
            // Calculate uptime (approximate - could be tracked more precisely)
            static auto start_time = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

            // Build provider status list
            nlohmann::json providers_json = nlohmann::json::array();
            size_t total_device_count = 0;

            for (const auto &[provider_id, provider] : providers_)
            {
                auto devices = registry_.get_devices_for_provider(provider_id);

                std::string state = provider->is_available() ? "AVAILABLE" : "UNAVAILABLE";

                providers_json.push_back({{"provider_id", provider_id},
                                          {"state", state},
                                          {"device_count", devices.size()}});

                total_device_count += devices.size();
            }

            // Get current mode from mode manager
            std::string current_mode = "MANUAL";
            if (mode_manager_)
            {
                current_mode = automation::mode_to_string(mode_manager_->current_mode());
            }

            nlohmann::json response = {
                {"status", make_status(StatusCode::OK)},
                {"mode", current_mode},
                {"uptime_seconds", uptime},
                {"polling_interval_ms", 500}, // TODO: Get from config
                {"providers", providers_json},
                {"device_count", total_device_count}};

            send_json(res, StatusCode::OK, response);
        }

        //=============================================================================
        // GET /v0/events (SSE - Server-Sent Events)
        //=============================================================================
        void HttpServer::handle_get_events(const httplib::Request &req, httplib::Response &res)
        {
            // Check if event emitter is available
            if (!event_emitter_)
            {
                nlohmann::json error_response = make_error_response(
                    StatusCode::UNAVAILABLE, "Event streaming not enabled");
                send_json(res, StatusCode::UNAVAILABLE, error_response);
                return;
            }

            // Check SSE client limit
            int current_clients = sse_client_count_.load();
            if (current_clients >= MAX_SSE_CLIENTS)
            {
                std::cerr << "[SSE] Client rejected: max clients (" << MAX_SSE_CLIENTS << ") reached\n";
                nlohmann::json error_response = make_error_response(
                    StatusCode::UNAVAILABLE, "Too many SSE clients");
                res.status = 503; // Service Unavailable
                res.set_content(error_response.dump(), "application/json");
                return;
            }

            // Parse optional filters
            events::EventFilter filter;
            if (req.has_param("provider_id"))
            {
                filter.provider_id = req.get_param_value("provider_id");
            }
            if (req.has_param("device_id"))
            {
                filter.device_id = req.get_param_value("device_id");
            }
            if (req.has_param("signal_id"))
            {
                filter.signal_id = req.get_param_value("signal_id");
            }

            // Subscribe to events (convert to shared_ptr for lambda capture)
            std::string client_name = "sse-" + std::to_string(current_clients + 1);
            std::shared_ptr<events::Subscription> subscription(
                event_emitter_->subscribe(filter, 100, client_name).release());

            if (!subscription)
            {
                std::cerr << "[SSE] Failed to create subscription\n";
                nlohmann::json error_response = make_error_response(
                    StatusCode::UNAVAILABLE, "Failed to subscribe to events");
                res.status = 503;
                res.set_content(error_response.dump(), "application/json");
                return;
            }

            sse_client_count_++;
            std::cerr << "[SSE] Client connected: " << client_name
                      << " (total: " << sse_client_count_.load() << ")\n";

            // Set SSE headers
            res.set_header("Content-Type", "text/event-stream");
            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");
            res.set_header("X-Accel-Buffering", "no"); // Disable nginx buffering

            // Per-client keep-alive counter (shared_ptr for lambda capture)
            auto keepalive_counter = std::make_shared<int>(0);

            // Use chunked content provider for streaming
            res.set_chunked_content_provider(
                "text/event-stream",
                [this, subscription, client_name, keepalive_counter](size_t offset, httplib::DataSink &sink)
                {
                    // Check if server is still running
                    if (!running_.load())
                    {
                        std::cerr << "[SSE] Server stopping, closing " << client_name << "\n";
                        return false; // Stop streaming
                    }

                    // Try to get event with timeout (allows periodic keep-alive)
                    auto event_opt = subscription->pop(1000); // 1 second timeout

                    if (event_opt)
                    {
                        // Format and send event
                        std::string sse_data = format_sse_event(*event_opt);
                        if (!sink.write(sse_data.c_str(), sse_data.size()))
                        {
                            std::cerr << "[SSE] Write failed for " << client_name << "\n";
                            return false;
                        }
                        *keepalive_counter = 0; // Reset on successful event
                    }
                    else
                    {
                        // No event - send keep-alive comment every ~15 seconds
                        if (++(*keepalive_counter) >= 15)
                        {
                            std::string keepalive = ": keepalive\n\n";
                            if (!sink.write(keepalive.c_str(), keepalive.size()))
                            {
                                std::cerr << "[SSE] Keep-alive failed for " << client_name << "\n";
                                return false;
                            }
                            *keepalive_counter = 0;
                        }
                    }

                    return true; // Continue streaming
                },
                [this, client_name](bool success)
                {
                    // Cleanup callback when stream ends
                    sse_client_count_--;
                    std::cerr << "[SSE] Client disconnected: " << client_name
                              << " (remaining: " << sse_client_count_.load() << ")\n";
                });
        }

        // Helper: Format event as SSE message
        std::string HttpServer::format_sse_event(const events::Event &event)
        {
            std::string result;

            std::visit([&result](auto &&e)
                       {
        using T = std::decay_t<decltype(e)>;
        
        nlohmann::json data;
        
        if constexpr (std::is_same_v<T, events::StateUpdateEvent>) {
            result = "event: state_update\n";
            result += "id: " + std::to_string(e.event_id) + "\n";
            
            data["provider_id"] = e.provider_id;
            data["device_id"] = e.device_id;
            data["signal_id"] = e.signal_id;
            data["timestamp_ms"] = e.timestamp_ms;
            data["quality"] = events::quality_to_string(e.quality);
            
            // Encode value in Phase 4 format
            std::visit([&data](auto&& val) {
                using V = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<V, double>) {
                    data["value"] = {{"type", "double"}, {"double", val}};
                } else if constexpr (std::is_same_v<V, int64_t>) {
                    data["value"] = {{"type", "int64"}, {"int64", val}};
                } else if constexpr (std::is_same_v<V, uint64_t>) {
                    data["value"] = {{"type", "uint64"}, {"uint64", val}};
                } else if constexpr (std::is_same_v<V, bool>) {
                    data["value"] = {{"type", "bool"}, {"bool", val}};
                } else if constexpr (std::is_same_v<V, std::string>) {
                    data["value"] = {{"type", "string"}, {"string", val}};
                }
            }, e.value);
            
        } else if constexpr (std::is_same_v<T, events::QualityChangeEvent>) {
            result = "event: quality_change\n";
            result += "id: " + std::to_string(e.event_id) + "\n";
            
            data["provider_id"] = e.provider_id;
            data["device_id"] = e.device_id;
            data["signal_id"] = e.signal_id;
            data["old_quality"] = events::quality_to_string(e.old_quality);
            data["new_quality"] = events::quality_to_string(e.new_quality);
            data["timestamp_ms"] = e.timestamp_ms;
            
        } else if constexpr (std::is_same_v<T, events::DeviceAvailabilityEvent>) {
            result = "event: device_availability\n";
            result += "id: " + std::to_string(e.event_id) + "\n";
            
            data["provider_id"] = e.provider_id;
            data["device_id"] = e.device_id;
            data["available"] = e.available;
            data["timestamp_ms"] = e.timestamp_ms;
        }
        
        result += "data: " + data.dump() + "\n\n"; }, event);

            return result;
        }

        //=============================================================================
        // GET /v0/mode - Get current automation mode
        //=============================================================================
        void HttpServer::handle_get_mode(const httplib::Request &req, httplib::Response &res)
        {
            // If automation not enabled, return error
            if (!mode_manager_)
            {
                nlohmann::json response = make_error_response(
                    StatusCode::UNAVAILABLE,
                    "Automation layer not enabled");
                send_json(res, StatusCode::UNAVAILABLE, response);
                return;
            }

            auto current = mode_manager_->current_mode();
            std::string mode_str = automation::mode_to_string(current);

            nlohmann::json response = {
                {"status", make_status(StatusCode::OK)},
                {"mode", mode_str}};

            send_json(res, StatusCode::OK, response);
        }

        //=============================================================================
        // POST /v0/mode - Set automation mode
        //=============================================================================
        void HttpServer::handle_post_mode(const httplib::Request &req, httplib::Response &res)
        {
            // If automation not enabled, return error
            if (!mode_manager_)
            {
                nlohmann::json response = make_error_response(
                    StatusCode::UNAVAILABLE,
                    "Automation layer not enabled");
                send_json(res, StatusCode::UNAVAILABLE, response);
                return;
            }

            // Parse request body
            nlohmann::json body;
            try
            {
                body = nlohmann::json::parse(req.body);
            }
            catch (const std::exception &e)
            {
                nlohmann::json response = make_error_response(
                    StatusCode::INVALID_ARGUMENT,
                    std::string("Invalid JSON: ") + e.what());
                send_json(res, StatusCode::INVALID_ARGUMENT, response);
                return;
            }

            // Validate required field
            if (!body.contains("mode") || !body["mode"].is_string())
            {
                nlohmann::json response = make_error_response(
                    StatusCode::INVALID_ARGUMENT,
                    "Missing or invalid 'mode' field (expected string: MANUAL, AUTO, IDLE, or FAULT)");
                send_json(res, StatusCode::INVALID_ARGUMENT, response);
                return;
            }

            std::string mode_str = body["mode"];

            // Validate mode string (must be exact match)
            if (mode_str != "MANUAL" && mode_str != "AUTO" && mode_str != "IDLE" && mode_str != "FAULT")
            {
                nlohmann::json response = make_error_response(
                    StatusCode::INVALID_ARGUMENT,
                    "Invalid mode: '" + mode_str + "' (must be MANUAL, AUTO, IDLE, or FAULT)");
                send_json(res, StatusCode::INVALID_ARGUMENT, response);
                return;
            }

            // Parse mode string
            auto new_mode = automation::string_to_mode(mode_str);

            // Attempt mode transition
            std::string error;
            if (!mode_manager_->set_mode(new_mode, error))
            {
                nlohmann::json response = make_error_response(
                    StatusCode::FAILED_PRECONDITION,
                    error);
                send_json(res, StatusCode::FAILED_PRECONDITION, response);
                return;
            }

            // Success - return new mode
            auto current = mode_manager_->current_mode();
            std::string current_str = automation::mode_to_string(current);

            nlohmann::json response = {
                {"status", make_status(StatusCode::OK)},
                {"mode", current_str}};

            send_json(res, StatusCode::OK, response);
        }

        //=============================================================================
        // GET /v0/parameters - Get all parameters (Phase 7C)
        //=============================================================================
        void HttpServer::handle_get_parameters(const httplib::Request &req, httplib::Response &res)
        {
            // If parameter manager not available, return error
            if (!parameter_manager_)
            {
                nlohmann::json response = make_error_response(
                    StatusCode::UNAVAILABLE,
                    "Parameter system not enabled");
                send_json(res, StatusCode::UNAVAILABLE, response);
                return;
            }

            // Get all parameter definitions
            auto params = parameter_manager_->get_all_definitions();

            nlohmann::json parameters_json = nlohmann::json::array();
            for (const auto &[name, def] : params)
            {
                nlohmann::json param_json = {
                    {"name", name},
                    {"type", automation::parameter_type_to_string(def.type)}};

                // Add current value based on type
                if (std::holds_alternative<double>(def.value))
                {
                    param_json["value"] = std::get<double>(def.value);
                }
                else if (std::holds_alternative<int64_t>(def.value))
                {
                    param_json["value"] = std::get<int64_t>(def.value);
                }
                else if (std::holds_alternative<bool>(def.value))
                {
                    param_json["value"] = std::get<bool>(def.value);
                }
                else if (std::holds_alternative<std::string>(def.value))
                {
                    param_json["value"] = std::get<std::string>(def.value);
                }

                // Add constraints if present
                if (def.min.has_value())
                {
                    param_json["min"] = def.min.value();
                }
                if (def.max.has_value())
                {
                    param_json["max"] = def.max.value();
                }
                if (def.allowed_values.has_value() && !def.allowed_values.value().empty())
                {
                    param_json["allowed_values"] = def.allowed_values.value();
                }

                parameters_json.push_back(param_json);
            }

            nlohmann::json response = {
                {"status", make_status(StatusCode::OK)},
                {"parameters", parameters_json}};

            send_json(res, StatusCode::OK, response);
        }

        //=============================================================================
        // POST /v0/parameters - Update parameter value (Phase 7C)
        //=============================================================================
        void HttpServer::handle_post_parameters(const httplib::Request &req, httplib::Response &res)
        {
            // If parameter manager not available, return error
            if (!parameter_manager_)
            {
                nlohmann::json response = make_error_response(
                    StatusCode::UNAVAILABLE,
                    "Parameter system not enabled");
                send_json(res, StatusCode::UNAVAILABLE, response);
                return;
            }

            // Parse request body
            nlohmann::json body;
            try
            {
                body = nlohmann::json::parse(req.body);
            }
            catch (const std::exception &e)
            {
                nlohmann::json response = make_error_response(
                    StatusCode::INVALID_ARGUMENT,
                    std::string("Invalid JSON: ") + e.what());
                send_json(res, StatusCode::INVALID_ARGUMENT, response);
                return;
            }

            // Validate required fields
            if (!body.contains("name") || !body["name"].is_string())
            {
                nlohmann::json response = make_error_response(
                    StatusCode::INVALID_ARGUMENT,
                    "Missing or invalid 'name' field (expected string)");
                send_json(res, StatusCode::INVALID_ARGUMENT, response);
                return;
            }

            if (!body.contains("value"))
            {
                nlohmann::json response = make_error_response(
                    StatusCode::INVALID_ARGUMENT,
                    "Missing 'value' field");
                send_json(res, StatusCode::INVALID_ARGUMENT, response);
                return;
            }

            std::string param_name = body["name"];

            // Get parameter definition to determine type
            auto def_opt = parameter_manager_->get_definition(param_name);
            if (!def_opt.has_value())
            {
                nlohmann::json response = make_error_response(
                    StatusCode::NOT_FOUND,
                    "Parameter '" + param_name + "' not found");
                send_json(res, StatusCode::NOT_FOUND, response);
                return;
            }

            const auto &def = def_opt.value();

            // Parse value based on parameter type
            automation::ParameterValue new_value;
            try
            {
                if (def.type == automation::ParameterType::DOUBLE)
                {
                    if (!body["value"].is_number())
                    {
                        throw std::runtime_error("Expected number for double parameter");
                    }
                    new_value = body["value"].get<double>();
                }
                else if (def.type == automation::ParameterType::INT64)
                {
                    if (!body["value"].is_number_integer())
                    {
                        throw std::runtime_error("Expected integer for int64 parameter");
                    }
                    new_value = body["value"].get<int64_t>();
                }
                else if (def.type == automation::ParameterType::BOOL)
                {
                    if (!body["value"].is_boolean())
                    {
                        throw std::runtime_error("Expected boolean for bool parameter");
                    }
                    new_value = body["value"].get<bool>();
                }
                else if (def.type == automation::ParameterType::STRING)
                {
                    if (!body["value"].is_string())
                    {
                        throw std::runtime_error("Expected string for string parameter");
                    }
                    new_value = body["value"].get<std::string>();
                }
            }
            catch (const std::exception &e)
            {
                nlohmann::json response = make_error_response(
                    StatusCode::INVALID_ARGUMENT,
                    std::string("Invalid value: ") + e.what());
                send_json(res, StatusCode::INVALID_ARGUMENT, response);
                return;
            }

            // Set parameter value with validation
            std::string error;
            if (!parameter_manager_->set(param_name, new_value, error))
            {
                nlohmann::json response = make_error_response(
                    StatusCode::INVALID_ARGUMENT,
                    error);
                send_json(res, StatusCode::INVALID_ARGUMENT, response);
                return;
            }

            // Success - return updated parameter
            auto updated_value = parameter_manager_->get(param_name).value();
            nlohmann::json value_json;
            if (std::holds_alternative<double>(updated_value))
            {
                value_json = std::get<double>(updated_value);
            }
            else if (std::holds_alternative<int64_t>(updated_value))
            {
                value_json = std::get<int64_t>(updated_value);
            }
            else if (std::holds_alternative<bool>(updated_value))
            {
                value_json = std::get<bool>(updated_value);
            }
            else if (std::holds_alternative<std::string>(updated_value))
            {
                value_json = std::get<std::string>(updated_value);
            }

            nlohmann::json response = {
                {"status", make_status(StatusCode::OK)},
                {"parameter", {{"name", param_name}, {"value", value_json}}}};

            send_json(res, StatusCode::OK, response);
        }

    } // namespace http
} // namespace anolis
