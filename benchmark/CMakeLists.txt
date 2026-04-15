# Copyright (c) Meta Platforms, Inc. and affiliates.

# The binary comes with an SAO corpus internally that isn't provided here
set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
FetchContent_Declare(
    googlebenchmark # v1.6.1
    URL https://github.com/google/benchmark/archive/refs/tags/v1.6.1.zip
    URL_HASH SHA256=367e963b8620080aff8c831e24751852cffd1f74ea40f25d9cc1b667a9dd5e45
    OVERRIDE_FIND_PACKAGE
)
FetchContent_Declare(
    fmt # v9.1.0
    URL https://github.com/fmtlib/fmt/releases/download/9.1.0/fmt-9.1.0.zip
    URL_HASH SHA256=cceb4cb9366e18a5742128cb3524ce5f50e88b476f1e54737a47ffdf4df4c996
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(googlebenchmark fmt)
find_package(googlebenchmark REQUIRED)
find_package(fmt REQUIRED)

file(
    GLOB_RECURSE openzl_benchmark_sources
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/unitBench/**/*.c")
list(FILTER openzl_benchmark_sources
     EXCLUDE REGEX "${CMAKE_CURRENT_LIST_DIR}/tools/.*$")
list(FILTER openzl_benchmark_sources
     EXCLUDE REGEX "${CMAKE_CURRENT_LIST_DIR}/BenchmarkComponents.cpp$")
file(
    GLOB_RECURSE openzl_benchmark_headers
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/unitBench/**/*.h")

add_executable(openzl_benchmark
    ${openzl_benchmark_sources}
    ${openzl_benchmark_headers}
)
target_link_libraries(openzl_benchmark
    PUBLIC
        openzl
        fileio
        benchmark::benchmark
        fmt::fmt
        ${OPENZL_FILESYSTEM_LIBRARY}
)
apply_openzl_compile_options_to_target(openzl_benchmark)

add_subdirectory(unitBench)
