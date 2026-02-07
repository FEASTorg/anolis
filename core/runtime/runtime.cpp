#include "runtime.hpp"

#include <chrono>
#include <thread>

#include "logging/logger.hpp"
#include "provider/provider_handle.hpp"  // Required for instantiation

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
    state_cache_->poll_once(providers_);

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

        // Discover devices
        if (!registry_->discover_provider(provider_config.id, *provider)) {
            error = "Discovery failed for provider '" + provider_config.id + "': " + registry_->last_error();
            return false;
        }

        providers_[provider_config.id] = provider;
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
        bt_runtime_ = std::make_unique<automation::BTRuntime>(*state_cache_, *call_router_, providers_, *mode_manager_,
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
            config_.http, *registry_, *state_cache_, *call_router_, providers_,
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
    state_cache_->start_polling(providers_);

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

    // Main loop (v0: just keep polling alive)
    // Future: HTTP server, BT engine, etc. will run here
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Check provider health
        for (const auto &[id, provider] : providers_) {
            if (!provider->is_available()) {
                LOG_WARN("[Runtime] Provider " << id << " unavailable");
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

    for (auto &[id, provider] : providers_) {
        LOG_INFO("[Runtime] Stopping provider: " << id);
        // ProviderHandle/ProviderProcess destructor handles cleanup
    }

    providers_.clear();
}

}  // namespace runtime
}  // namespace anolis
