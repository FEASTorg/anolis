#include "runtime.hpp"

#include <chrono>
#include <thread>

#include "logging/logger.hpp"
#include "provider/provider_handle.hpp"  // Required for instantiation
#include "provider/provider_supervisor.hpp"
#include "signal_handler.hpp"

namespace anolis {
namespace runtime {

Runtime::Runtime(const RuntimeConfig &config) : config_(config) {}

Runtime::~Runtime() { shutdown(); }

bool Runtime::initialize(std::string &error) {
    LOG_INFO("[Runtime] Initializing Anolis Core");

    if (!init_core_services(error)) {
        return false;
    }

    if (!init_providers(error)) {
        return false;
    }

    if (!state_cache_->initialize()) {
        error = "State cache initialization failed: " + state_cache_->last_error();
        return false;
    }

    // Prime state cache once so initial HTTP calls observe a full snapshot
    state_cache_->poll_once(provider_registry_);

    if (!init_automation(error)) {
        return false;
    }

    if (!init_http(error)) {
        return false;
    }

    if (!init_telemetry(error)) {
        return false;
    }

    LOG_INFO("[Runtime] Initialization complete");
    return true;
}

bool Runtime::init_core_services(std::string &error) {
    // Create registry
    registry_ = std::make_unique<registry::DeviceRegistry>();

    // Create event emitter
    // Default: 100 events per subscriber queue, max 32 SSE clients
    event_emitter_ = std::make_shared<events::EventEmitter>(100, 32);
    LOG_INFO("[Runtime] Event emitter created (max " << event_emitter_->max_subscribers() << " subscribers)");

    // Create state cache
    state_cache_ = std::make_unique<state::StateCache>(*registry_, config_.polling.interval_ms);

    // Wire event emitter to state cache
    state_cache_->set_event_emitter(event_emitter_);

    // Create call router
    call_router_ = std::make_unique<control::CallRouter>(*registry_, *state_cache_);

    // Create provider supervisor
    supervisor_ = std::make_unique<provider::ProviderSupervisor>();
    LOG_INFO("[Runtime] Provider supervisor created");

    return true;
}

bool Runtime::init_providers(std::string &error) {
    // Start all providers and discover
    for (const auto &provider_config : config_.providers) {
        LOG_INFO("[Runtime] Starting provider: " << provider_config.id);
        LOG_DEBUG("[Runtime]   Command: " << provider_config.command);

        auto provider = std::make_shared<provider::ProviderHandle>(provider_config.id, provider_config.command,
                                                                   provider_config.args, provider_config.timeout_ms);

        if (!provider->start()) {
            error = "Failed to start provider '" + provider_config.id + "': " + provider->last_error();
            return false;
        }

        LOG_INFO("[Runtime] Provider " << provider_config.id << " started");

        // Register provider with supervisor
        supervisor_->register_provider(provider_config.id, provider_config.restart_policy);

        // Discover devices
        if (!registry_->discover_provider(provider_config.id, *provider)) {
            error = "Discovery failed for provider '" + provider_config.id + "': " + registry_->last_error();
            return false;
        }

        provider_registry_.add_provider(provider_config.id, provider);
    }

    LOG_INFO("[Runtime] All providers started");
    return true;
}

bool Runtime::init_automation(std::string &error) {
    // Create ModeManager and wire to CallRouter if automation enabled
    if (config_.automation.enabled) {
        auto initial_mode = config_.runtime.mode;
        mode_manager_ = std::make_unique<automation::ModeManager>(initial_mode);

        std::string policy_str =
            (config_.automation.manual_gating_policy == GatingPolicy::BLOCK) ? "BLOCK" : "OVERRIDE";
        call_router_->set_mode_manager(mode_manager_.get(), policy_str);
    }

    // Create and initialize ParameterManager BEFORE HTTP server
    if (config_.automation.enabled) {
        LOG_INFO("[Runtime] Creating parameter manager");
        parameter_manager_ = std::make_unique<automation::ParameterManager>();

        // Load parameters from config
        for (const auto &param_config : config_.automation.parameters) {
            automation::ParameterType type;
            if (param_config.type == "double") {
                type = automation::ParameterType::DOUBLE;
            } else if (param_config.type == "int64") {
                type = automation::ParameterType::INT64;
            } else if (param_config.type == "bool") {
                type = automation::ParameterType::BOOL;
            } else if (param_config.type == "string") {
                type = automation::ParameterType::STRING;
            } else {
                LOG_WARN("[Runtime] Invalid parameter type: " << param_config.type);
                continue;
            }

            automation::ParameterValue value;
            if (param_config.type == "double") {
                value = param_config.double_value;
            } else if (param_config.type == "int64") {
                value = param_config.int64_value;
            } else if (param_config.type == "bool") {
                value = param_config.bool_value;
            } else if (param_config.type == "string") {
                value = param_config.string_value;
            }

            std::optional<double> min =
                param_config.has_min ? std::make_optional(param_config.min_value) : std::nullopt;
            std::optional<double> max =
                param_config.has_max ? std::make_optional(param_config.max_value) : std::nullopt;
            std::optional<std::vector<std::string>> allowed =
                param_config.allowed_values.empty() ? std::nullopt : std::make_optional(param_config.allowed_values);

            if (!parameter_manager_->define(param_config.name, type, value, min, max, allowed)) {
                LOG_WARN("[Runtime] Failed to define parameter: " << param_config.name);
            }
        }

        LOG_INFO("[Runtime] Parameter manager initialized with " << parameter_manager_->parameter_count()
                                                                 << " parameters");
    }

    // Register mode change callback to emit telemetry events (only if automation enabled)
    if (mode_manager_) {
        mode_manager_->on_mode_change([this](automation::RuntimeMode prev, automation::RuntimeMode next) {
            if (event_emitter_) {
                events::ModeChangeEvent event;
                event.event_id = event_emitter_->next_event_id();
                event.previous_mode = automation::mode_to_string(prev);
                event.new_mode = automation::mode_to_string(next);
                event.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count();

                event_emitter_->emit(event);
                LOG_INFO("[Runtime] Mode change event emitted: " << event.previous_mode << " -> " << event.new_mode);
            }
        });
    }

    // Register callback to emit parameter change events (if parameter manager exists)
    if (parameter_manager_) {
        parameter_manager_->on_parameter_change([this](const std::string &name,
                                                       const automation::ParameterValue &old_value,
                                                       const automation::ParameterValue &new_value) {
            if (event_emitter_) {
                // Convert values to strings for telemetry
                auto value_to_string = [](const automation::ParameterValue &v) -> std::string {
                    if (std::holds_alternative<double>(v)) {
                        return std::to_string(std::get<double>(v));
                    } else if (std::holds_alternative<int64_t>(v)) {
                        return std::to_string(std::get<int64_t>(v));
                    } else if (std::holds_alternative<bool>(v)) {
                        return std::get<bool>(v) ? "true" : "false";
                    } else if (std::holds_alternative<std::string>(v)) {
                        return std::get<std::string>(v);
                    }
                    return "";
                };

                events::ParameterChangeEvent event;
                event.event_id = event_emitter_->next_event_id();
                event.parameter_name = name;
                event.old_value_str = value_to_string(old_value);
                event.new_value_str = value_to_string(new_value);
                event.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count();

                event_emitter_->emit(event);
                LOG_INFO("[Runtime] Parameter '" << name << "' changed: " << event.old_value_str << " -> "
                                                 << event.new_value_str);
            }
        });
    }

    // Create and initialize BTRuntime if enabled
    if (config_.automation.enabled) {
        LOG_INFO("[Runtime] Creating BT runtime");
        bt_runtime_ =
            std::make_unique<automation::BTRuntime>(*state_cache_, *call_router_, provider_registry_, *mode_manager_,
                                                    parameter_manager_.get()  // Pass parameter manager
            );

        if (!bt_runtime_->load_tree(config_.automation.behavior_tree)) {
            error = "Failed to load behavior tree: " + config_.automation.behavior_tree;
            return false;
        }

        LOG_INFO("[Runtime] Behavior tree loaded: " << config_.automation.behavior_tree);
    } else {
        LOG_INFO("[Runtime] Automation disabled in config");
    }

    return true;
}

bool Runtime::init_http(std::string &error) {
    // Create and start HTTP server if enabled
    if (config_.http.enabled) {
        LOG_INFO("[Runtime] Creating HTTP server");
        http_server_ = std::make_unique<http::HttpServer>(
            config_.http, config_.polling.interval_ms, *registry_, *state_cache_, *call_router_, provider_registry_,
            event_emitter_,           // Pass event emitter for SSE
            mode_manager_.get(),      // Pass mode manager (nullptr if automation disabled)
            parameter_manager_.get()  // Pass parameter manager (nullptr if automation disabled)
        );

        std::string http_error;
        if (!http_server_->start(http_error)) {
            error = "HTTP server failed to start: " + http_error;
            return false;
        }
        LOG_INFO("[Runtime] HTTP server started on " << config_.http.bind << ":" << config_.http.port);
    } else {
        LOG_INFO("[Runtime] HTTP server disabled in config");
    }
    return true;
}

bool Runtime::init_telemetry(std::string &error) {
    // Start telemetry sink if enabled
    if (config_.telemetry.enabled) {
        LOG_INFO("[Runtime] Creating telemetry sink");

        telemetry::InfluxConfig influx_config;
        influx_config.enabled = true;
        influx_config.url = config_.telemetry.influx_url;
        influx_config.org = config_.telemetry.influx_org;
        influx_config.bucket = config_.telemetry.influx_bucket;
        influx_config.token = config_.telemetry.influx_token;
        influx_config.batch_size = config_.telemetry.batch_size;
        influx_config.flush_interval_ms = config_.telemetry.flush_interval_ms;
        influx_config.queue_size = config_.telemetry.queue_size;

        telemetry_sink_ = std::make_unique<telemetry::InfluxSink>(influx_config);

        if (!telemetry_sink_->start(event_emitter_)) {
            LOG_WARN("[Runtime] Telemetry sink failed to start");
            // Don't fail runtime initialization - telemetry is optional
        } else {
            LOG_INFO("[Runtime] Telemetry sink started");
        }
    } else {
        LOG_INFO("[Runtime] Telemetry disabled in config");
    }
    return true;
}

void Runtime::run() {
    LOG_INFO("[Runtime] Starting main loop");
    running_ = true;

    // Start state cache polling
    state_cache_->start_polling(provider_registry_);

    LOG_INFO("[Runtime] State cache polling active");

    // Start BT tick loop if automation enabled
    if (bt_runtime_) {
        if (!bt_runtime_->start(config_.automation.tick_rate_hz)) {
            LOG_WARN("[Runtime] BT runtime failed to start");
        } else {
            LOG_INFO("[Runtime] BT runtime started (tick rate: " << config_.automation.tick_rate_hz << " Hz)");
        }
    }

    LOG_INFO("[Runtime] Press Ctrl+C to exit");

    // Main loop: polling, provider health monitoring, crash recovery
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Check for shutdown signal
        if (anolis::runtime::SignalHandler::is_shutdown_requested()) {
            LOG_INFO("[Runtime] Signal received, stopping...");
            running_ = false;
            break;
        }

        // Check provider health and handle restarts
        for (const auto &provider_config : config_.providers) {
            const std::string &id = provider_config.id;
            auto provider = provider_registry_.get_provider(id);

            if (!provider) {
                continue;  // Provider not initialized (shouldn't happen)
            }

            // Check if provider is available
            if (!provider->is_available()) {
                // Provider crashed or unavailable

                // Check if circuit breaker is open
                if (supervisor_->is_circuit_open(id)) {
                    continue;  // Circuit open, no more attempts
                }

                // Mark that we've detected this crash (only records once per crash)
                if (supervisor_->mark_crash_detected(id)) {
                    // This is a new crash - record it and schedule restart
                    if (!supervisor_->record_crash(id)) {
                        // Max attempts exceeded - circuit breaker opened
                        continue;
                    }
                }

                // Check if we should attempt restart (backoff period elapsed)
                if (supervisor_->should_restart(id)) {
                    // Clear crash detected flag before restart attempt
                    // (so next crash will be properly detected)
                    supervisor_->clear_crash_detected(id);

                    // Attempt restart
                    LOG_INFO("[Runtime] Attempting to restart provider: " << id);
                    if (restart_provider(id, provider_config)) {
                        LOG_INFO("[Runtime] Provider restarted successfully: " << id);
                        supervisor_->record_success(id);
                    } else {
                        LOG_ERROR("[Runtime] Failed to restart provider: " << id);
                        // Don't mark crash detected - will retry on next iteration
                    }
                }
            } else {
                // Provider is healthy - ensure supervisor knows about recovery
                if (supervisor_->get_attempt_count(id) > 0) {
                    supervisor_->record_success(id);
                }
            }
        }
    }

    LOG_INFO("[Runtime] Shutting down");
    state_cache_->stop_polling();
}

void Runtime::shutdown() {
    // Stop BT runtime first
    if (bt_runtime_) {
        LOG_INFO("[Runtime] Stopping BT runtime");
        bt_runtime_->stop();
    }

    // Stop HTTP server
    if (http_server_) {
        LOG_INFO("[Runtime] Stopping HTTP server");
        http_server_->stop();
    }

    // Stop telemetry sink
    if (telemetry_sink_) {
        LOG_INFO("[Runtime] Stopping telemetry sink");
        telemetry_sink_->stop();
    }

    if (state_cache_) {
        state_cache_->stop_polling();
    }

    // Provider cleanup handled by ProviderRegistry destructor
    provider_registry_.clear();
}

bool Runtime::restart_provider(const std::string &provider_id, const ProviderConfig &provider_config) {
    // Remove old provider instance
    provider_registry_.remove_provider(provider_id);

    // Clear devices from registry
    registry_->clear_provider_devices(provider_id);

    LOG_INFO("[Runtime] Restarting provider: " << provider_id);
    LOG_DEBUG("[Runtime]   Command: " << provider_config.command);

    // Create new provider instance
    auto provider = std::make_shared<provider::ProviderHandle>(provider_id, provider_config.command,
                                                               provider_config.args, provider_config.timeout_ms);

    if (!provider->start()) {
        LOG_ERROR("[Runtime] Failed to start provider '" << provider_id << "': " << provider->last_error());
        return false;
    }

    LOG_INFO("[Runtime] Provider " << provider_id << " process started");

    // Rediscover devices
    if (!registry_->discover_provider(provider_id, *provider)) {
        LOG_ERROR("[Runtime] Discovery failed for provider '" << provider_id << "': " << registry_->last_error());
        return false;
    }

    // Rebuild poll configs for this provider (Sprint 1.3: reconcile changed capabilities)
    state_cache_->rebuild_poll_configs(provider_id);

    // Re-add to provider registry
    provider_registry_.add_provider(provider_id, provider);

    LOG_INFO("[Runtime] Provider " << provider_id << " restarted and devices rediscovered");
    return true;
}

}  // namespace runtime
}  // namespace anolis
