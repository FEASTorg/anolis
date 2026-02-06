#include "../server.hpp"
#include "utils.hpp"
#include "../json.hpp"
#include "../../registry/device_registry.hpp"
#include "../../logging/logger.hpp"

namespace anolis
{
    namespace http
    {

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

    } // namespace http
} // namespace anolis
