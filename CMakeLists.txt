# Anolis Runtime - CMake Build Configuration
cmake_minimum_required(VERSION 3.20)
project(anolis LANGUAGES CXX)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find protobuf via vcpkg
find_package(Protobuf CONFIG REQUIRED)

# Structural Analysis
option(ENABLE_CLANG_TIDY "Enable static analysis with clang-tidy" OFF)

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
    if(CLANG_TIDY_EXE)
        # We use the inline integration for MSBuild/VS compatibility
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        message(STATUS "clang-tidy configured for build-time analysis")
    else()
        message(WARNING "clang-tidy not found but ENABLE_CLANG_TIDY is ON")
    endif()
endif()

# Coverage Analysis (GCC/Clang)
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
        # Multithreaded tests can corrupt gcov counters without atomic updates,
        # which causes lcov/geninfo failures with negative branch counts.
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            add_compile_options(-fprofile-update=atomic)
        endif()
        add_link_options(--coverage)
        message(STATUS "Code coverage enabled (gcov/lcov compatible)")
    elseif(MSVC)
        message(WARNING "Coverage with MSVC requires Visual Studio profiler tools, not gcov/lcov")
    else()
        message(WARNING "Coverage requested but compiler not supported: ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

# ThreadSanitizer (TSAN) - Data Race Detection
option(ENABLE_TSAN "Enable ThreadSanitizer for data race detection" OFF)

if(ENABLE_TSAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        if(NOT WIN32)
            add_compile_options(-fsanitize=thread -g -fno-omit-frame-pointer)
            add_link_options(-fsanitize=thread)
            message(STATUS "ThreadSanitizer (TSAN) enabled for data race detection")
        else()
            message(WARNING "ThreadSanitizer not supported on Windows (Linux/macOS only)")
        endif()
    else()
        message(WARNING "ThreadSanitizer requires GCC or Clang, current compiler: ${CMAKE_CXX_COMPILER_ID}")
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
