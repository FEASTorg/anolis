# Anolis Runtime - CMake Build Configuration
cmake_minimum_required(VERSION 3.20)
project(anolis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find protobuf via vcpkg
find_package(Protobuf CONFIG REQUIRED)

# Structural Analysis
option(ENABLE_CLANG_TIDY "Enable static analysis with clang-tidy" OFF)
if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "clang-tidy not found but ENABLE_CLANG_TIDY is ON")
    endif()
endif()

# Subdirectories
add_subdirectory(core)

# Enable testing
option(BUILD_TESTING "Build unit tests" ON)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests/unit)
endif()
