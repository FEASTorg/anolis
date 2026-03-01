#include "../../control/call_router.hpp"
#include "../../logging/logger.hpp"
#include "../json.hpp"
#include "../server.hpp"
#include "utils.hpp"

namespace anolis {
namespace http {

//=============================================================================
// POST /v0/call
//=============================================================================
void HttpServer::handle_post_call(const httplib::Request &req, httplib::Response &res) {
    try {
        // Parse request body
        nlohmann::json request_json;
        try {
            request_json = nlohmann::json::parse(req.body);
        } catch (const std::exception &e) {
            send_json(res, StatusCode::INVALID_ARGUMENT,
                      make_error_response(StatusCode::INVALID_ARGUMENT, std::string("Invalid JSON: ") + e.what()));
            return;
        }

        // Decode transport payload only; entity/function/arg semantics are validated in CallRouter.
        std::string provider_id, device_id;
        uint32_t function_id;
        std::map<std::string, anolis::deviceprovider::v1::Value> args;
        std::string error;

        if (!decode_call_request(request_json, provider_id, device_id, function_id, args, error)) {
            send_json(res, StatusCode::INVALID_ARGUMENT, make_error_response(StatusCode::INVALID_ARGUMENT, error));
            return;
        }

        // Build CallRequest
        control::CallRequest call_request;
        call_request.device_handle = provider_id + "/" + device_id;
        call_request.function_id = function_id;
        call_request.args = args;

        // Execute call
        auto result = call_router_.execute_call(call_request, provider_registry_);

        if (!result.success) {
            // Map ADPP status code to internal StatusCode
            StatusCode status = StatusCode::INTERNAL;

            switch (result.status_code) {
                case anolis::deviceprovider::v1::Status_Code_CODE_INVALID_ARGUMENT:
                case anolis::deviceprovider::v1::Status_Code_CODE_OUT_OF_RANGE:
                    status = StatusCode::INVALID_ARGUMENT;
                    break;
                case anolis::deviceprovider::v1::Status_Code_CODE_NOT_FOUND:
                    status = StatusCode::NOT_FOUND;
                    break;
                case anolis::deviceprovider::v1::Status_Code_CODE_FAILED_PRECONDITION:
                    status = StatusCode::FAILED_PRECONDITION;
                    break;
                case anolis::deviceprovider::v1::Status_Code_CODE_UNAVAILABLE:
                case anolis::deviceprovider::v1::Status_Code_CODE_RESOURCE_EXHAUSTED:
                    status = StatusCode::UNAVAILABLE;
                    break;
                case anolis::deviceprovider::v1::Status_Code_CODE_DEADLINE_EXCEEDED:
                    status = StatusCode::DEADLINE_EXCEEDED;
                    break;
                default:
                    status = StatusCode::INTERNAL;
                    break;
            }

            LOG_ERROR("[HTTP] Call failed: " << result.error_message << " (Code: " << result.status_code
                                             << "), returning " << static_cast<int>(status));
            // std::flush is implied by logging
            send_json(res, status, make_error_response(status, result.error_message));
            return;
        }

        nlohmann::json response = {{"status", make_status(StatusCode::OK)},
                                   {"provider_id", provider_id},
                                   {"device_id", device_id},
                                   {"function_id", function_id},
                                   {"post_call_poll_triggered", true}};

        send_json(res, StatusCode::OK, response);
    } catch (const std::exception &e) {
        LOG_ERROR("[HTTP] Exception in handle_post_call: " << e.what());
        send_json(res, StatusCode::INTERNAL,
                  make_error_response(StatusCode::INTERNAL, std::string("Exception: ") + e.what()));
    }
}

}  // namespace http
}  // namespace anolis
