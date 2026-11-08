cmake_minimum_required(VERSION 3.24)
project(TickMatch VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(TICKMATCH_BUILD_TESTS "Build unit and integration tests" ON)
option(TICKMATCH_BUILD_BENCHMARKS "Build Google Benchmark suite" ON)
option(TICKMATCH_BUILD_APPS "Build the replay application" ON)
set(TICKMATCH_SANITIZE "" CACHE STRING "Sanitizers to enable: address,undefined | thread | empty")

add_library(tickmatch_warnings INTERFACE)
target_compile_options(tickmatch_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion
                                                   -Wshadow -Werror)

if(TICKMATCH_SANITIZE)
    add_compile_options(-fsanitize=${TICKMATCH_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${TICKMATCH_SANITIZE})
endif()

add_subdirectory(src)

if(TICKMATCH_BUILD_APPS)
    add_subdirectory(apps)
endif()

if(TICKMATCH_BUILD_TESTS OR TICKMATCH_BUILD_BENCHMARKS)
    include(FetchContent)
endif()

if(TICKMATCH_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    add_subdirectory(tests)
endif()
