cmake_minimum_required(VERSION 3.20)
project(lob CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Dependencies — use locally installed GoogleTest
include(cmake/googletest.cmake)

# ZeroMQ (system install via Homebrew)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

# nlohmann/json (system install via Homebrew)
find_package(nlohmann_json REQUIRED)

# Enable testing
enable_testing()

# Core library + matching engine
add_subdirectory(src)

# Unit tests
add_subdirectory(tests)

# CLI tools
add_subdirectory(tools)

# Benchmarks
add_subdirectory(benchmarks)
