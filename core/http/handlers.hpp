#pragma once

/**
 * @brief HTTP Route Handlers
 *
 * All route handlers are defined as methods on HttpServer.
 * Implementation is in handlers.cpp.
 *
 * Endpoints (v0):
 * - GET  /v0/devices                               -> handle_get_devices
 * - GET  /v0/devices/{provider_id}/{device_id}/capabilities -> handle_get_device_capabilities
 * - GET  /v0/state                                 -> handle_get_state
 * - GET  /v0/state/{provider_id}/{device_id}       -> handle_get_device_state
 * - POST /v0/call                                  -> handle_post_call
 * - GET  /v0/runtime/status                        -> handle_get_runtime_status
 *
 * All responses use JSON and include a top-level "status" object.
 */

// Handler implementations are part of HttpServer class in server.hpp/server.cpp
// This header exists for documentation and potential future refactoring.
