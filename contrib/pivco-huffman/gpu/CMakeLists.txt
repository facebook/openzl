# Copyright (c) Meta Platforms, Inc. and affiliates.

cmake_minimum_required(VERSION 3.26)
project(pivco_gpu_contrib LANGUAGES C CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# A standalone build for the existing contrib tests and benchmark. The main
# OpenZL build and its production codec remain unchanged.
get_filename_component(OPENZL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
option(PIVCO_GPU_BUILD_TESTS "Build the PivCo contrib tests" ON)
set(PIVCO_GPU_BASELINE_SOURCE "" CACHE FILEPATH
    "Optional earlier pivco_gpu.cu, built with the same core and benchmark")

set(OPENZL_BUILD_ALL OFF CACHE BOOL "")
set(OPENZL_BUILD_TESTS OFF CACHE BOOL "")
set(OPENZL_BUILD_CPP OFF CACHE BOOL "")
set(OPENZL_BUILD_CUSTOM_PARSERS OFF CACHE BOOL "")
set(OPENZL_BUILD_TOOLS OFF CACHE BOOL "")
set(OPENZL_BUILD_CLI OFF CACHE BOOL "")
set(OPENZL_BUILD_EXAMPLES OFF CACHE BOOL "")
set(OPENZL_INSTALL OFF CACHE BOOL "")
add_subdirectory("${OPENZL_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/openzl" EXCLUDE_FROM_ALL)

find_package(CUDAToolkit REQUIRED)
function(add_pivco_gpu_library name cuda_source)
    add_library(${name} STATIC "${cuda_source}" pivco_gpu_context.cpp pivco_block_index.cpp)
    target_compile_options(${name} PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)
    target_link_libraries(${name} PUBLIC openzl CUDA::cudart)
endfunction()
add_pivco_gpu_library(pivco_gpu "${CMAKE_CURRENT_SOURCE_DIR}/pivco_gpu.cu")

add_executable(pivco_gpu_bench PivCoGpuBench.cpp)
target_link_libraries(pivco_gpu_bench PRIVATE pivco_gpu)
if(PIVCO_GPU_BASELINE_SOURCE)
    add_pivco_gpu_library(pivco_gpu_baseline "${PIVCO_GPU_BASELINE_SOURCE}")
    add_executable(pivco_gpu_bench_baseline PivCoGpuBench.cpp)
    target_link_libraries(pivco_gpu_bench_baseline PRIVATE pivco_gpu_baseline)
endif()

if(PIVCO_GPU_BUILD_TESTS)
    # Match the version and archive identity used by the root test build.
    include(FetchContent)
    set(INSTALL_GTEST OFF)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
        URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    add_executable(pivco_gpu_test PivCoGpuTest.cpp)
    target_link_libraries(pivco_gpu_test PRIVATE pivco_gpu GTest::gtest_main)
    add_executable(pivco_block_index_test PivCoBlockIndexTest.cpp)
    target_link_libraries(pivco_block_index_test PRIVATE pivco_gpu GTest::gtest_main)
    add_test(NAME pivco_gpu_test COMMAND pivco_gpu_test)
    add_test(NAME pivco_block_index_test COMMAND pivco_block_index_test)
    if(PIVCO_GPU_BASELINE_SOURCE)
        # Manual negative control: the old implementation should fail the
        # new full-block regression. Do not add this target to normal CTest.
        add_executable(pivco_gpu_regression_baseline PivCoGpuTest.cpp)
        target_link_libraries(pivco_gpu_regression_baseline PRIVATE
            pivco_gpu_baseline GTest::gtest_main)
    endif()
endif()
