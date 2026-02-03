#include "server.hpp"
#include "errors.hpp"
#include <iostream>

namespace anolis {
namespace http {

HttpServer::HttpServer(const runtime::HttpConfig& config,
                       registry::DeviceRegistry& registry,
                       state::StateCache& state_cache,
                       control::CallRouter& call_router,
                       ProviderMap& providers)
    : config_(config)
    , registry_(registry)
    , state_cache_(state_cache)
    , call_router_(call_router)
    , providers_(providers) {}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::start(std::string& error) {
    if (running_.load()) {
        error = "Server already running";
        return false;
    }
    
    std::cerr << "[HTTP] Starting server on " << config_.bind << ":" << config_.port << "\n";
    
    // Create server
    server_ = std::make_unique<httplib::Server>();
    
    // Configure server
    server_->set_read_timeout(5, 0);   // 5 seconds
    server_->set_write_timeout(5, 0);  // 5 seconds
    
    // Set up routes
    setup_routes();
    
    // Set error handler for JSON error responses (called for HTTP errors like 404)
    // Only override content if no content has been set
    server_->set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        // If content was already set by the handler, don't override it
        if (!res.body.empty()) {
            return;
        }
        
        StatusCode code = StatusCode::INTERNAL;
        std::string message = "Internal server error";
        
        if (res.status == 404) {
            code = StatusCode::NOT_FOUND;
            message = "Route not found: " + req.method + " " + req.path;
        } else if (res.status == 400) {
            code = StatusCode::INVALID_ARGUMENT;
            message = "Bad request";
        } else if (res.status == 500) {
            code = StatusCode::INTERNAL;
            message = "Internal server error";
        }
        
        nlohmann::json response = make_error_response(code, message);
        res.set_content(response.dump(), "application/json");
    });
    
    // Set exception handler
    server_->set_exception_handler([](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
        std::string msg = "Unknown error";
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            msg = e.what();
            std::cerr << "[HTTP] Exception: " << e.what() << "\n" << std::flush;
        } catch (...) {
            msg = "Unknown exception";
            std::cerr << "[HTTP] Unknown exception\n" << std::flush;
        }
        
        nlohmann::json response = make_error_response(StatusCode::INTERNAL, msg);
        res.status = 500;
        res.set_content(response.dump(), "application/json");
    });
    
    // Bind first (bind_to_port returns the actual port used, or -1 on error)
    if (!server_->bind_to_port(config_.bind.c_str(), config_.port)) {
        error = "Failed to bind to " + config_.bind + ":" + std::to_string(config_.port);
        return false;
    }
    port_ = config_.port;
    
    // Start server thread
    running_.store(true);
    server_thread_ = std::make_unique<std::thread>([this]() {
        std::cerr << "[HTTP] Server thread started\n" << std::flush;
        server_->listen_after_bind();
        std::cerr << "[HTTP] Server thread exiting\n";
    });
    
    std::cerr << "[HTTP] Server listening on " << config_.bind << ":" << config_.port << "\n" << std::flush;
    return true;
}

void HttpServer::stop() {
    if (!running_.load()) {
        return;
    }
    
    std::cerr << "[HTTP] Stopping server\n";
    running_.store(false);
    
    if (server_) {
        server_->stop();
    }
    
    if (server_thread_ && server_thread_->joinable()) {
        server_thread_->join();
    }
    
    server_thread_.reset();
    server_.reset();
    std::cerr << "[HTTP] Server stopped\n";
}

void HttpServer::setup_routes() {
    // GET /v0/devices - List all devices
    server_->Get("/v0/devices", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_devices(req, res);
    });
    
    // GET /v0/devices/:provider_id/:device_id/capabilities - Get device capabilities
    server_->Get(R"(/v0/devices/([^/]+)/([^/]+)/capabilities)", 
                 [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_device_capabilities(req, res);
    });
    
    // GET /v0/state - Get all device state
    server_->Get("/v0/state", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_state(req, res);
    });
    
    // GET /v0/state/:provider_id/:device_id - Get specific device state
    server_->Get(R"(/v0/state/([^/]+)/([^/]+))", 
                 [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_device_state(req, res);
    });
    
    // POST /v0/call - Execute device function
    server_->Post("/v0/call", [this](const httplib::Request& req, httplib::Response& res) {
        handle_post_call(req, res);
    });
    
    // GET /v0/runtime/status - Get runtime status
    server_->Get("/v0/runtime/status", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_runtime_status(req, res);
    });
    
    std::cerr << "[HTTP] Routes configured:\n";
    std::cerr << "[HTTP]   GET  /v0/devices\n";
    std::cerr << "[HTTP]   GET  /v0/devices/{provider_id}/{device_id}/capabilities\n";
    std::cerr << "[HTTP]   GET  /v0/state\n";
    std::cerr << "[HTTP]   GET  /v0/state/{provider_id}/{device_id}\n";
    std::cerr << "[HTTP]   POST /v0/call\n";
    std::cerr << "[HTTP]   GET  /v0/runtime/status\n";
}

} // namespace http
} // namespace anolis
