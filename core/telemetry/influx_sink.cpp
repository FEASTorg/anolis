/**
 * @file influx_sink.cpp
 * @brief Implementation of InfluxDB telemetry sink
 */

#include "influx_sink.hpp"

#include "logging/logger.hpp"

// cpp-httplib with OpenSSL support (enabled via vcpkg feature)
#include <httplib.h>

#include <chrono>

namespace anolis {
namespace telemetry {

void InfluxSink::flush_batch() {
    // Move batch out under lock (minimize lock time)
    std::vector<std::string> lines_to_write;
    {
        std::lock_guard<std::mutex> lock(batch_mutex_);
        if (batch_.empty()) {
            return;
        }
        lines_to_write = std::move(batch_);
        batch_.clear();
    }

    // Build line protocol body (newline separated)
    std::string body;
    size_t total_size = 0;
    for (const auto &line : lines_to_write) {
        total_size += line.size() + 1;  // +1 for newline
    }
    body.reserve(total_size);

    for (const auto &line : lines_to_write) {
        body += line;
        body += '\n';
    }

    // Create HTTP/HTTPS client from URL
    // httplib::Client auto-detects scheme and handles SSL when built with OpenSSL
    auto client = std::make_unique<httplib::Client>(config_.url);

    client->set_connection_timeout(std::chrono::milliseconds(config_.timeout_ms));
    client->set_read_timeout(std::chrono::milliseconds(config_.timeout_ms));
    client->set_write_timeout(std::chrono::milliseconds(config_.timeout_ms));

    // Build URL path with query params
    std::string path = "/api/v2/write?org=" + config_.org + "&bucket=" + config_.bucket + "&precision=ms";

    // Set authorization header
    httplib::Headers headers = {{"Authorization", "Token " + config_.token},
                                {"Content-Type", "text/plain; charset=utf-8"}};

    // Send request
    auto result = client->Post(path.c_str(), headers, body, "text/plain");

    if (result) {
        if (result->status >= 200 && result->status < 300) {
            // Success
            connected_.store(true);
            total_written_.fetch_add(lines_to_write.size());

            // Log periodically (every 1000 writes)
            if (total_written_.load() % 1000 == 0) {
                LOG_INFO("[InfluxSink] Written " << total_written_.load() << " events to InfluxDB");
            }
        } else {
            // HTTP error
            connected_.store(false);
            total_failed_.fetch_add(lines_to_write.size());

            // Rate-limit error logging
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_error_log_).count();

            if (elapsed >= 10) {  // Log at most every 10 seconds
                LOG_WARN("[InfluxSink] HTTP error " << result->status << ": " << result->body);
                last_error_log_ = now;
            }
        }
    } else {
        // Connection error
        connected_.store(false);
        total_failed_.fetch_add(lines_to_write.size());

        // Rate-limit error logging
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_error_log_).count();

        if (elapsed >= 10) {
            auto err = result.error();
            LOG_ERROR("[InfluxSink] Connection error: " << httplib::to_string(err));
        }
    }
}

}  // namespace telemetry
}  // namespace anolis
