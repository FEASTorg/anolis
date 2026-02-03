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
#include <chrono>

namespace anolis {
namespace http {

// Helper: Parse provider_id and device_id from regex matches
static bool parse_path_params(const httplib::Request& req, 
                              std::string& provider_id, 
                              std::string& device_id) {
    if (req.matches.size() >= 3) {
        provider_id = req.matches[1].str();
        device_id = req.matches[2].str();
        return true;
    }
    return false;
}

// Helper: Send JSON response
static void send_json(httplib::Response& res, StatusCode code, const nlohmann::json& body) {
    res.status = status_code_to_http(code);
    res.set_content(body.dump(), "application/json");
}

//=============================================================================
// GET /v0/devices
//=============================================================================
void HttpServer::handle_get_devices(const httplib::Request& req, httplib::Response& res) {
    auto devices = registry_.get_all_devices();
    
    nlohmann::json devices_json = nlohmann::json::array();
    for (const auto* device : devices) {
        devices_json.push_back(encode_device_info(*device));
    }
    
    nlohmann::json response = {
        {"status", make_status(StatusCode::OK)},
        {"devices", devices_json}
    };
    
    send_json(res, StatusCode::OK, response);
}

//=============================================================================
// GET /v0/devices/{provider_id}/{device_id}/capabilities
//=============================================================================
void HttpServer::handle_get_device_capabilities(const httplib::Request& req, httplib::Response& res) {
    std::string provider_id, device_id;
    if (!parse_path_params(req, provider_id, device_id)) {
        send_json(res, StatusCode::INVALID_ARGUMENT, 
                  make_error_response(StatusCode::INVALID_ARGUMENT, "Invalid path parameters"));
        return;
    }
    
    const auto* device = registry_.get_device(provider_id, device_id);
    if (!device) {
        send_json(res, StatusCode::NOT_FOUND,
                  make_error_response(StatusCode::NOT_FOUND, 
                                     "Device not found: " + provider_id + "/" + device_id));
        return;
    }
    
    nlohmann::json response = {
        {"status", make_status(StatusCode::OK)},
        {"provider_id", provider_id},
        {"device_id", device_id},
        {"capabilities", encode_capabilities(device->capabilities)}
    };
    
    send_json(res, StatusCode::OK, response);
}

//=============================================================================
// GET /v0/state
//=============================================================================
void HttpServer::handle_get_state(const httplib::Request& req, httplib::Response& res) {
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    auto devices = registry_.get_all_devices();
    nlohmann::json devices_json = nlohmann::json::array();
    
    for (const auto* device : devices) {
        auto state = state_cache_.get_device_state(device->get_handle());
        if (state) {
            devices_json.push_back(
                encode_device_state(*state, device->provider_id, device->device_id));
        }
    }
    
    nlohmann::json response = {
        {"status", make_status(StatusCode::OK)},
        {"generated_at_epoch_ms", now_ms},
        {"devices", devices_json}
    };
    
    send_json(res, StatusCode::OK, response);
}

//=============================================================================
// GET /v0/state/{provider_id}/{device_id}
//=============================================================================
void HttpServer::handle_get_device_state(const httplib::Request& req, httplib::Response& res) {
    std::string provider_id, device_id;
    if (!parse_path_params(req, provider_id, device_id)) {
        send_json(res, StatusCode::INVALID_ARGUMENT,
                  make_error_response(StatusCode::INVALID_ARGUMENT, "Invalid path parameters"));
        return;
    }
    
    const auto* device = registry_.get_device(provider_id, device_id);
    if (!device) {
        send_json(res, StatusCode::NOT_FOUND,
                  make_error_response(StatusCode::NOT_FOUND,
                                     "Device not found: " + provider_id + "/" + device_id));
        return;
    }
    
    auto state = state_cache_.get_device_state(device->get_handle());
    if (!state) {
        // Device exists but no state yet (shouldn't normally happen)
        send_json(res, StatusCode::UNAVAILABLE,
                  make_error_response(StatusCode::UNAVAILABLE, "Device state not available"));
        return;
    }
    
    // Handle optional signal_id query params
    std::vector<std::string> filter_signals;
    for (size_t i = 0; req.has_param("signal_id"); i++) {
        // Collect all signal_id params
        auto it = req.params.find("signal_id");
        if (it != req.params.end()) {
            // Get all values for signal_id
            auto range = req.params.equal_range("signal_id");
            for (auto it2 = range.first; it2 != range.second; ++it2) {
                filter_signals.push_back(it2->second);
            }
            break;
        }
    }
    
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    // Build filtered or full state
    nlohmann::json values = nlohmann::json::array();
    
    auto worst_quality = anolis::deviceprovider::v0::SignalValue_Quality_QUALITY_OK;
    
    for (const auto& [signal_id, cached] : state->signals) {
        // Apply filter if specified
        if (!filter_signals.empty()) {
            bool found = false;
            for (const auto& fs : filter_signals) {
                if (fs == signal_id) {
                    found = true;
                    break;
                }
            }
            if (!found) continue;
        }
        
        values.push_back(encode_signal_value(cached, signal_id));
        if (cached.quality > worst_quality) {
            worst_quality = cached.quality;
        }
    }
    
    std::string device_quality = state->provider_available ?
        quality_to_string(worst_quality) : "UNAVAILABLE";
    
    nlohmann::json response = {
        {"status", make_status(StatusCode::OK)},
        {"generated_at_epoch_ms", now_ms},
        {"provider_id", provider_id},
        {"device_id", device_id},
        {"quality", device_quality},
        {"values", values}
    };
    
    send_json(res, StatusCode::OK, response);
}

//=============================================================================
// POST /v0/call
//=============================================================================
void HttpServer::handle_post_call(const httplib::Request& req, httplib::Response& res) {
    try {
        // Parse request body
        nlohmann::json request_json;
        try {
            request_json = nlohmann::json::parse(req.body);
        } catch (const std::exception& e) {
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
        
        if (!decode_call_request(request_json, provider_id, device_id, function_id, args, error)) {
            send_json(res, StatusCode::INVALID_ARGUMENT,
                      make_error_response(StatusCode::INVALID_ARGUMENT, error));
            return;
        }
        
        // Check if provider exists
        auto provider_it = providers_.find(provider_id);
        if (provider_it == providers_.end()) {
            send_json(res, StatusCode::NOT_FOUND,
                      make_error_response(StatusCode::NOT_FOUND,
                                         "Provider not found: " + provider_id));
            return;
        }
        
        // Check if provider is available
        if (!provider_it->second->is_available()) {
            send_json(res, StatusCode::UNAVAILABLE,
                      make_error_response(StatusCode::UNAVAILABLE,
                                         "Provider unavailable: " + provider_id));
            return;
        }
        
        // Look up device to get function name
        const auto* device = registry_.get_device(provider_id, device_id);
        if (!device) {
            send_json(res, StatusCode::NOT_FOUND,
                      make_error_response(StatusCode::NOT_FOUND,
                                         "Device not found: " + provider_id + "/" + device_id));
            return;
        }
        
        // Find function by ID
        std::string function_name;
        for (const auto& [fname, spec] : device->capabilities.functions_by_id) {
            if (spec.function_id == function_id) {
                function_name = fname;
                break;
            }
        }
        
        if (function_name.empty()) {
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
        
        if (!result.success) {
            // Determine error type
            StatusCode status = StatusCode::INTERNAL;
            if (result.error_message.find("not found") != std::string::npos ||
                result.error_message.find("does not exist") != std::string::npos) {
                status = StatusCode::NOT_FOUND;
            } else if (result.error_message.find("argument") != std::string::npos ||
                       result.error_message.find("parameter") != std::string::npos ||
                       result.error_message.find("validation") != std::string::npos) {
                status = StatusCode::INVALID_ARGUMENT;
            } else if (result.error_message.find("timeout") != std::string::npos ||
                       result.error_message.find("deadline") != std::string::npos) {
                status = StatusCode::DEADLINE_EXCEEDED;
            } else if (result.error_message.find("unavailable") != std::string::npos) {
                status = StatusCode::UNAVAILABLE;
            }
            
            std::cerr << "[HTTP] Call failed: " << result.error_message << ", returning " << static_cast<int>(status) << "\n" << std::flush;
            send_json(res, status, make_error_response(status, result.error_message));
            return;
        }
        
        nlohmann::json response = {
            {"status", make_status(StatusCode::OK)},
            {"provider_id", provider_id},
            {"device_id", device_id},
            {"function_id", function_id},
            {"post_call_poll_triggered", true}
        };
        
        send_json(res, StatusCode::OK, response);
    } catch (const std::exception& e) {
        std::cerr << "[HTTP] Exception in handle_post_call: " << e.what() << "\n" << std::flush;
        send_json(res, StatusCode::INTERNAL, 
                  make_error_response(StatusCode::INTERNAL, std::string("Exception: ") + e.what()));
    }
}

//=============================================================================
// GET /v0/runtime/status
//=============================================================================
void HttpServer::handle_get_runtime_status(const httplib::Request& req, httplib::Response& res) {
    // Calculate uptime (approximate - could be tracked more precisely)
    static auto start_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
    
    // Build provider status list
    nlohmann::json providers_json = nlohmann::json::array();
    size_t total_device_count = 0;
    
    for (const auto& [provider_id, provider] : providers_) {
        auto devices = registry_.get_devices_for_provider(provider_id);
        
        std::string state = provider->is_available() ? "AVAILABLE" : "UNAVAILABLE";
        
        providers_json.push_back({
            {"provider_id", provider_id},
            {"state", state},
            {"device_count", devices.size()}
        });
        
        total_device_count += devices.size();
    }
    
    nlohmann::json response = {
        {"status", make_status(StatusCode::OK)},
        {"mode", "MANUAL"},  // Always MANUAL in v0
        {"uptime_seconds", uptime},
        {"polling_interval_ms", 500},  // TODO: Get from config
        {"providers", providers_json},
        {"device_count", total_device_count}
    };
    
    send_json(res, StatusCode::OK, response);
}

} // namespace http
} // namespace anolis
