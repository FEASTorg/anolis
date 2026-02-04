#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace anolis
{
    namespace http
    {

        /**
         * @brief ADPP status codes mapped to HTTP status codes
         *
         * Status codes mirror the ADPP protocol status model:
         * - OK -> HTTP 200
         * - INVALID_ARGUMENT -> HTTP 400
         * - NOT_FOUND -> HTTP 404
         * - FAILED_PRECONDITION -> HTTP 409
         * - UNAVAILABLE -> HTTP 503
         * - DEADLINE_EXCEEDED -> HTTP 504
         * - INTERNAL -> HTTP 500
         */
        enum class StatusCode
        {
            OK,
            INVALID_ARGUMENT,
            NOT_FOUND,
            FAILED_PRECONDITION,
            UNAVAILABLE,
            DEADLINE_EXCEEDED,
            INTERNAL
        };

        /**
         * @brief Convert StatusCode to HTTP status integer
         */
        inline int status_code_to_http(StatusCode code)
        {
            switch (code)
            {
            case StatusCode::OK:
                return 200;
            case StatusCode::INVALID_ARGUMENT:
                return 400;
            case StatusCode::NOT_FOUND:
                return 404;
            case StatusCode::FAILED_PRECONDITION:
                return 409;
            case StatusCode::UNAVAILABLE:
                return 503;
            case StatusCode::DEADLINE_EXCEEDED:
                return 504;
            case StatusCode::INTERNAL:
                return 500;
            default:
                return 500;
            }
        }

        /**
         * @brief Convert StatusCode to string representation
         */
        inline std::string status_code_to_string(StatusCode code)
        {
            switch (code)
            {
            case StatusCode::OK:
                return "OK";
            case StatusCode::INVALID_ARGUMENT:
                return "INVALID_ARGUMENT";
            case StatusCode::NOT_FOUND:
                return "NOT_FOUND";
            case StatusCode::FAILED_PRECONDITION:
                return "FAILED_PRECONDITION";
            case StatusCode::UNAVAILABLE:
                return "UNAVAILABLE";
            case StatusCode::DEADLINE_EXCEEDED:
                return "DEADLINE_EXCEEDED";
            case StatusCode::INTERNAL:
                return "INTERNAL";
            default:
                return "INTERNAL";
            }
        }

        /**
         * @brief Build a JSON status object
         *
         * All HTTP responses include a top-level "status" object with code and message.
         * This mirrors the ADPP status model for consistency.
         */
        inline nlohmann::json make_status(StatusCode code, const std::string &message = "")
        {
            std::string msg = message.empty() ? (code == StatusCode::OK ? "ok" : status_code_to_string(code)) : message;
            return {
                {"code", status_code_to_string(code)},
                {"message", msg}};
        }

        /**
         * @brief Build a complete JSON error response
         *
         * Creates a JSON object with just the status field for error responses.
         */
        inline nlohmann::json make_error_response(StatusCode code, const std::string &message)
        {
            return {
                {"status", make_status(code, message)}};
        }

    } // namespace http
} // namespace anolis
