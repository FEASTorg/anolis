# Anolis Core - Runtime Executable

# Generate protobuf from spec (same as provider-sim)
set(ADPP_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../spec/device-provider)

# Use modern protobuf_generate instead of deprecated protobuf_generate_cpp
add_library(anolis_proto OBJECT ${ADPP_PROTO_DIR}/protocol.proto)
target_link_libraries(anolis_proto PUBLIC protobuf::libprotobuf)
target_include_directories(anolis_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

protobuf_generate(
    TARGET anolis_proto
    LANGUAGE cpp
    IMPORT_DIRS ${ADPP_PROTO_DIR}
    PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

# Provider host library
add_library(anolis_provider STATIC
    provider/framed_stdio_client.cpp
    provider/provider_process.cpp
    provider/provider_handle.cpp
)

target_include_directories(anolis_provider PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(anolis_provider PUBLIC
    anolis_proto
    protobuf::libprotobuf
)

# Registry library
add_library(anolis_registry STATIC
    registry/device_registry.cpp
)

target_include_directories(anolis_registry PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(anolis_registry PUBLIC
    anolis_proto
    anolis_provider
    protobuf::libprotobuf
)

# Events library (Phase 6 - header-only for now)
add_library(anolis_events INTERFACE)

target_include_directories(anolis_events INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Telemetry library (Phase 6B - InfluxDB sink)
add_library(anolis_telemetry STATIC
    telemetry/influx_sink.cpp
)

target_include_directories(anolis_telemetry PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(anolis_telemetry PUBLIC
    anolis_events
    httplib::httplib
)

# Automation library (Phase 7 - Behavior Trees)
find_package(behaviortree_cpp CONFIG REQUIRED)

add_library(anolis_automation STATIC
    automation/bt_runtime.cpp
    automation/bt_nodes.cpp
    automation/mode_manager.cpp
    automation/parameter_manager.cpp
)

target_include_directories(anolis_automation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(anolis_automation PUBLIC
    anolis_state
    anolis_control
    BT::behaviortree_cpp
)

# Automation sanity test (lightweight, no gtest)
add_executable(bt_nodes_sanity
    automation/bt_nodes_sanity.cpp
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_libraries(bt_nodes_sanity PRIVATE
        $<LINK_GROUP:RESCAN,anolis_automation,anolis_control>
        BT::behaviortree_cpp
    )
else()
    target_link_libraries(bt_nodes_sanity PRIVATE
        anolis_automation
        BT::behaviortree_cpp
    )
endif()

# State cache library
add_library(anolis_state STATIC
    state/state_cache.cpp
)

target_include_directories(anolis_state PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(anolis_state PUBLIC
    anolis_proto
    anolis_provider
    anolis_registry
    anolis_events
    protobuf::libprotobuf
)

# Call router library
add_library(anolis_control STATIC
    control/call_router.cpp
)

target_include_directories(anolis_control PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(anolis_control PUBLIC
    anolis_proto
    anolis_provider
    anolis_registry
    anolis_state
    protobuf::libprotobuf
)

# HTTP server library (Phase 4)
find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(anolis_http STATIC
    http/json.cpp
    http/server.cpp
    http/handlers.cpp
)

target_include_directories(anolis_http PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(anolis_http PUBLIC
    anolis_proto
    anolis_provider
    anolis_registry
    anolis_state
    anolis_control
    httplib::httplib
    nlohmann_json::nlohmann_json
    protobuf::libprotobuf
)

# Runtime library (config + bootstrap)
find_package(yaml-cpp CONFIG REQUIRED)

add_library(anolis_runtime STATIC
    runtime/config.cpp
    runtime/runtime.cpp
)

target_include_directories(anolis_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(anolis_runtime PUBLIC
    anolis_proto
    anolis_provider
    anolis_registry
    anolis_state
    anolis_control
    anolis_http
    anolis_telemetry
    anolis_automation
    yaml-cpp::yaml-cpp
    protobuf::libprotobuf
)

# Minimal runtime executable
add_executable(anolis-runtime
    src/main.cpp
)

target_include_directories(anolis-runtime PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}  # Generated headers
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(anolis-runtime PRIVATE
    anolis_runtime
    anolis_provider
    anolis_registry
    anolis_state
    anolis_control
    anolis_http
    anolis_proto
    yaml-cpp::yaml-cpp
    protobuf::libprotobuf
)

if(WIN32)
    target_compile_definitions(anolis-runtime PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
