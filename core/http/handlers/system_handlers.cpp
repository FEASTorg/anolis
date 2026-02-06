#include "../server.hpp"
#include "utils.hpp"
#include "../json.hpp"
#include "../../registry/device_registry.hpp"
#include "../../automation/mode_manager.hpp"
#include "../../automation/parameter_manager.hpp"
#include "../../provider/i_provider_handle.hpp" // Need provider definition for handle_get_runtime_status
#include "../../logging/logger.hpp"
#include <chrono>

namespace anolis
{
    namespace http
    {

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
        // GET /v0/parameters - Get all parameters
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
        // POST /v0/parameters - Update parameter value
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
