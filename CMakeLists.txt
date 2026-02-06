# Anolis Runtime - CMake Build Configuration
cmake_minimum_required(VERSION 3.20)
project(anolis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find protobuf via vcpkg
find_package(Protobuf CONFIG REQUIRED)

# Enable testing
enable_testing()

# Subdirectories
add_subdirectory(core)
add_subdirectory(tests)
