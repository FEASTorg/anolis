# Anolis Core — Runtime Executable

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

# Minimal runtime executable
add_executable(anolis-runtime
    src/main.cpp
)

target_include_directories(anolis-runtime PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}  # Generated headers
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(anolis-runtime PRIVATE
    anolis_proto
    protobuf::libprotobuf
)

if(WIN32)
    target_compile_definitions(anolis-runtime PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
