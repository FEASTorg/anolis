# Anolis Unit Tests

find_package(GTest CONFIG REQUIRED)

# Test executable
add_executable(anolis_unit_tests
    main.cpp
    sanity_test.cpp
    state_cache_test.cpp
    call_router_test.cpp
    config_test.cpp
    http_handlers_test.cpp
    mode_manager_test.cpp
    provider_process_test.cpp
    provider_registry_test.cpp
    device_registry_concurrency_test.cpp
    event_emitter_test.cpp
    influx_sink_test.cpp
)

target_include_directories(anolis_unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core  # Access to internal headers
    ${CMAKE_BINARY_DIR}/core  # Access to generated proto headers
)

target_link_libraries(anolis_unit_tests PRIVATE
    GTest::gtest
    GTest::gmock
    anolis_runtime
    anolis_state
    anolis_control
    anolis_automation
    anolis_provider
    anolis_registry
    anolis_http
    anolis_events
    anolis_telemetry
    anolis_proto
    protobuf::libprotobuf
)

# Register tests. Use PRE_TEST so discovery does not execute during build.
include(GoogleTest)
gtest_discover_tests(anolis_unit_tests
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
)
