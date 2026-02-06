#pragma once

#include <string>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include "../errors.hpp"

namespace anolis
{
    namespace http
    {

        // Helper: Parse provider_id and device_id from regex matches
        inline bool parse_path_params(const httplib::Request &req,
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
        inline void send_json(httplib::Response &res, StatusCode code, const nlohmann::json &body)
        {
            res.status = status_code_to_http(code);
            res.set_content(body.dump(), "application/json");
        }

    } // namespace http
} // namespace anolis
