#pragma once

// Prevent Windows macro pollution (must be before httplib.h)
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include <memory>
#include <thread>
#include <atomic>
#include <string>
#include <unordered_map>
#include <httplib.h>
#include "runtime/config.hpp"
#include "provider/provider_handle.hpp"
#include "events/event_types.hpp"

// Forward declarations
namespace anolis {
namespace runtime { class Runtime; }
namespace registry { class DeviceRegistry; }
namespace state { class StateCache; }
namespace control { class CallRouter; }
namespace events { class EventEmitter; }
}

// Typedef for provider map
using ProviderMap = std::unordered_map<std::string, std::shared_ptr<anolis::provider::ProviderHandle>>;

namespace anolis {
namespace http {

/**
 * @brief HTTP server wrapper for Anolis runtime
 * 
 * The HTTP server is an external adapter layer that exposes the
 * runtime's capabilities over REST endpoints. It runs in a separate
 * thread and delegates all operations to the kernel components
 * (Registry, StateCache, CallRouter).
 * 
 * Thread model:
 * - Server runs in its own thread (via httplib::Server::listen_after_bind)
 * - Request handlers execute in httplib's thread pool
 * - All kernel operations are thread-safe (polling uses atomics/mutexes)
 * 
 * Lifecycle:
 * - start() binds to configured port and spawns server thread
 * - stop() signals shutdown and joins server thread
 */
class HttpServer {
public:
    /**
     * @brief Construct HTTP server with references to kernel components
     * 
     * @param config HTTP configuration (bind address, port)
     * @param registry Device registry for inventory/capabilities
     * @param state_cache State cache for reading device state
     * @param call_router Call router for executing device functions
     * @param providers Provider map for call execution
     * @param event_emitter Event emitter for SSE streaming (Phase 6)
     */
    HttpServer(const runtime::HttpConfig& config,
               registry::DeviceRegistry& registry,
               state::StateCache& state_cache,
               control::CallRouter& call_router,
               ProviderMap& providers,
               std::shared_ptr<events::EventEmitter> event_emitter = nullptr);
    
    ~HttpServer();
    
    /**
     * @brief Start HTTP server
     * 
     * Binds to configured address/port and starts server thread.
     * Returns true if server started successfully.
     * 
     * @param error Populated with error message on failure
     * @return true if server started
     */
    bool start(std::string& error);
    
    /**
     * @brief Stop HTTP server
     * 
     * Signals shutdown and waits for server thread to exit.
     * Safe to call multiple times.
     */
    void stop();
    
    /**
     * @brief Check if server is running
     */
    bool is_running() const { return running_.load(); }
    
    /**
     * @brief Get the port server is listening on
     */
    int get_port() const { return port_; }
    
private:
    // Configuration
    runtime::HttpConfig config_;
    int port_ = 0;
    
    // Kernel component references
    registry::DeviceRegistry& registry_;
    state::StateCache& state_cache_;
    control::CallRouter& call_router_;
    ProviderMap& providers_;
    std::shared_ptr<events::EventEmitter> event_emitter_;  // Phase 6 SSE
    
    // SSE client tracking
    std::atomic<int> sse_client_count_{0};
    static constexpr int MAX_SSE_CLIENTS = 32;
    
    // Server state
    std::unique_ptr<httplib::Server> server_;
    std::unique_ptr<std::thread> server_thread_;
    std::atomic<bool> running_{false};
    
    // Route setup
    void setup_routes();
    
    // Route handlers (implemented in handlers.cpp)
    void handle_get_devices(const httplib::Request& req, httplib::Response& res);
    void handle_get_device_capabilities(const httplib::Request& req, httplib::Response& res);
    void handle_get_state(const httplib::Request& req, httplib::Response& res);
    void handle_get_device_state(const httplib::Request& req, httplib::Response& res);
    void handle_post_call(const httplib::Request& req, httplib::Response& res);
    void handle_get_runtime_status(const httplib::Request& req, httplib::Response& res);
    
    // SSE handler (Phase 6)
    void handle_get_events(const httplib::Request& req, httplib::Response& res);
    std::string format_sse_event(const events::Event& event);
};

} // namespace http
} // namespace anolis
