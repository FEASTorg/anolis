#include "server.hpp"
#include "errors.hpp"
#include "events/event_emitter.hpp"
#include "logging/logger.hpp"

namespace anolis
{
    namespace http
    {

        HttpServer::HttpServer(const runtime::HttpConfig &config,
                               registry::DeviceRegistry &registry,
                               state::StateCache &state_cache,
                               control::CallRouter &call_router,
                               ProviderMap &providers,
                               std::shared_ptr<events::EventEmitter> event_emitter,
                               automation::ModeManager *mode_manager,
                               automation::ParameterManager *parameter_manager)
            : config_(config), registry_(registry), state_cache_(state_cache),
              call_router_(call_router), providers_(providers), event_emitter_(event_emitter),
              mode_manager_(mode_manager), parameter_manager_(parameter_manager) {}

        HttpServer::~HttpServer()
        {
            stop();
        }

        bool HttpServer::start(std::string &error)
        {
            if (running_.load())
            {
                error = "Server already running";
                return false;
            }

            LOG_INFO("[HTTP] Starting server on " << config_.bind << ":" << config_.port);

            // Create server
            server_ = std::make_unique<httplib::Server>();

            // Configure server
            server_->set_read_timeout(5, 0);  // 5 seconds
            server_->set_write_timeout(5, 0); // 5 seconds

            // Set thread pool size to handle SSE + REST concurrently
            // Rule: thread_pool_size >= max_sse_clients + headroom for REST
            // Default is 40 threads (32 SSE clients + 8 headroom)
            int pool_size = config_.thread_pool_size;
            server_->new_task_queue = [pool_size]
            { return new httplib::ThreadPool(pool_size); };

            // Add CORS headers to all responses
            std::string cors_origin = config_.cors_origin;
            server_->set_post_routing_handler([cors_origin](const httplib::Request &req, httplib::Response &res)
                                              {
        res.set_header("Access-Control-Allow-Origin", cors_origin.c_str());
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type"); });

            // Set up routes
            setup_routes();

            // Set error handler for JSON error responses (called for HTTP errors like 404)
            // Only override content if no content has been set
            server_->set_error_handler([](const httplib::Request &req, httplib::Response &res)
                                       {
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
        res.set_content(response.dump(), "application/json"); });

            // Set exception handler
            server_->set_exception_handler([](const httplib::Request &req, httplib::Response &res, std::exception_ptr ep)
                                           {
        std::string msg = "Unknown error";
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            msg = e.what();
            LOG_ERROR("[HTTP] Exception: " << e.what());
        } catch (...) {
            msg = "Unknown exception";
            LOG_ERROR("[HTTP] Unknown exception");
        }
        
        nlohmann::json response = make_error_response(StatusCode::INTERNAL, msg);
        res.status = 500;
        res.set_content(response.dump(), "application/json"); });

            // Bind first (bind_to_port returns the actual port used, or -1 on error)
            if (!server_->bind_to_port(config_.bind.c_str(), config_.port))
            {
                error = "Failed to bind to " + config_.bind + ":" + std::to_string(config_.port);
                return false;
            }
            port_ = config_.port;

            // Start server thread
            running_.store(true);
            server_thread_ = std::make_unique<std::thread>([this]()
                                                           {
        LOG_INFO("[HTTP] Server thread started");
        server_->listen_after_bind();
        LOG_INFO("[HTTP] Server thread exiting"); });

            LOG_INFO("[HTTP] Server listening on " << config_.bind << ":" << config_.port);
            return true;
        }

        void HttpServer::stop()
        {
            if (!running_.load())
            {
                return;
            }

            LOG_INFO("[HTTP] Stopping server");
            running_.store(false);

            if (server_)
            {
                server_->stop();
            }

            if (server_thread_ && server_thread_->joinable())
            {
                server_thread_->join();
            }

            server_thread_.reset();
            server_.reset();
            LOG_INFO("[HTTP] Server stopped");
        }

        void HttpServer::setup_routes()
        {
            // GET /v0/devices - List all devices
            server_->Get("/v0/devices", [this](const httplib::Request &req, httplib::Response &res)
                         { handle_get_devices(req, res); });

            // GET /v0/devices/:provider_id/:device_id/capabilities - Get device capabilities
            server_->Get(R"(/v0/devices/([^/]+)/([^/]+)/capabilities)",
                         [this](const httplib::Request &req, httplib::Response &res)
                         {
                             handle_get_device_capabilities(req, res);
                         });

            // GET /v0/state - Get all device state
            server_->Get("/v0/state", [this](const httplib::Request &req, httplib::Response &res)
                         { handle_get_state(req, res); });

            // GET /v0/state/:provider_id/:device_id - Get specific device state
            server_->Get(R"(/v0/state/([^/]+)/([^/]+))",
                         [this](const httplib::Request &req, httplib::Response &res)
                         {
                             handle_get_device_state(req, res);
                         });

            // POST /v0/call - Execute device function
            server_->Post("/v0/call", [this](const httplib::Request &req, httplib::Response &res)
                          { handle_post_call(req, res); });

            // OPTIONS /v0/call - CORS preflight for POST
            server_->Options("/v0/call", [](const httplib::Request &req, httplib::Response &res)
                             {
                                 res.status = 204; // No content
                             });

            // OPTIONS catch-all for CORS preflight on all routes
            server_->Options(R"(/v0/.*)", [](const httplib::Request &req, httplib::Response &res)
                             {
                                 res.status = 204; // No content
                             });

            // GET /v0/runtime/status - Get runtime status
            server_->Get("/v0/runtime/status", [this](const httplib::Request &req, httplib::Response &res)
                         { handle_get_runtime_status(req, res); });

            // GET /v0/mode - Get current automation mode
            server_->Get("/v0/mode", [this](const httplib::Request &req, httplib::Response &res)
                         { handle_get_mode(req, res); });

            // POST /v0/mode - Set automation mode
            server_->Post("/v0/mode", [this](const httplib::Request &req, httplib::Response &res)
                          { handle_post_mode(req, res); });

            // GET /v0/parameters - Get all parameters
            server_->Get("/v0/parameters", [this](const httplib::Request &req, httplib::Response &res)
                         { handle_get_parameters(req, res); });

            // POST /v0/parameters - Update parameter value
            server_->Post("/v0/parameters", [this](const httplib::Request &req, httplib::Response &res)
                          { handle_post_parameters(req, res); });

            // GET /v0/events - SSE event stream
            server_->Get("/v0/events", [this](const httplib::Request &req, httplib::Response &res)
                         { handle_get_events(req, res); });

            LOG_INFO("[HTTP] Routes configured:");
            LOG_INFO("[HTTP]   GET  /v0/devices");
            LOG_INFO("[HTTP]   GET  /v0/devices/{provider_id}/{device_id}/capabilities");
            LOG_INFO("[HTTP]   GET  /v0/state");
            LOG_INFO("[HTTP]   GET  /v0/state/{provider_id}/{device_id}");
            LOG_INFO("[HTTP]   POST /v0/call");
            LOG_INFO("[HTTP]   GET  /v0/runtime/status");
            LOG_INFO("[HTTP]   GET  /v0/mode");
            LOG_INFO("[HTTP]   POST /v0/mode");
            LOG_INFO("[HTTP]   GET  /v0/parameters");
            LOG_INFO("[HTTP]   POST /v0/parameters");
            LOG_INFO("[HTTP]   GET  /v0/events (SSE)");
        }

    } // namespace http
} // namespace anolis
