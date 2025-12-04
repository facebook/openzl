# Copyright (c) Meta Platforms, Inc. and affiliates.

if (OPENZL_SANITIZE_ADDRESS)
    if (("${CMAKE_CXX_COMPILER_ID}" MATCHES GNU) OR ("${CMAKE_CXX_COMPILER_ID}" MATCHES Clang))
        set(OPENZL_SANITIZE_ADDRESS ON)
        set(OPENZL_ASAN_FLAGS -fsanitize=address,undefined)
        list(APPEND OPENZL_COMMON_FLAGS ${OPENZL_ASAN_FLAGS})
    endif()
endif()

if (OPENZL_SANITIZE_MEMORY)
    if (("${CMAKE_CXX_COMPILER_ID}" MATCHES GNU) OR ("${CMAKE_CXX_COMPILER_ID}" MATCHES Clang))
        set(OPENZL_SANITIZE_MEMORY ON)
        set(OPENZL_MSAN_FLAGS -fsanitize=memory)
        list(APPEND OPENZL_COMMON_FLAGS ${OPENZL_MSAN_FLAGS})
    endif()
endif()

set(ZSTD_LEGACY_SUPPORT OFF)

# Two-tier zstd dependency resolution with automated hash verification
# 1. Git submodule (matches Makefile behavior)
# 2. FetchContent + URL (no git required, cryptographically verified)

set(ZSTD_VERSION "1.5.7")
set(ZSTD_DIRNAME "zstd-${ZSTD_VERSION}")
set(ZSTD_TARBALL_SHA256 "eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3")
set(ZSTD_EXPECTED_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/deps/zstd/lib/zstd.h")
set(ZSTD_EXPECTED_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/deps/zstd/build/cmake/CMakeLists.txt")

# Helper function to check if zstd is available and working
function(check_zstd_available RESULT_VAR)
    if(EXISTS "${ZSTD_EXPECTED_HEADER}" AND EXISTS "${ZSTD_EXPECTED_CMAKE}")
        set(${RESULT_VAR} TRUE PARENT_SCOPE)
    else()
        set(${RESULT_VAR} FALSE PARENT_SCOPE)
    endif()
endfunction()

message(STATUS "Attempting zstd dependency resolution...")

# Check if zstd is already available
check_zstd_available(ZSTD_AVAILABLE)
if(ZSTD_AVAILABLE)
    message(STATUS "zstd dependency already present")
else()
    # Tier 1: Git Submodule (existing approach, matches Makefile)
    message(STATUS "Tier 1: Trying git submodule...")
    execute_process(
        COMMAND git submodule update --init --single-branch --depth 1 deps/zstd
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE GIT_SUBMOD_RESULT
        OUTPUT_QUIET ERROR_QUIET
    )

    check_zstd_available(ZSTD_AVAILABLE)
endif()

# Tier 2: FetchContent + URL with Hash Verification
if(NOT ZSTD_AVAILABLE)
    message(STATUS "Tier 1 failed. Tier 2: Trying FetchContent with verified tarball...")

    include(FetchContent)

    # Clean up any partial downloads first
    file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/deps/zstd")
    file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/deps/${ZSTD_DIRNAME}")

    message(STATUS "Using hash verification for tarball download")
    FetchContent_Declare(zstd_tarball
        URL https://github.com/facebook/zstd/releases/download/v${ZSTD_VERSION}/${ZSTD_DIRNAME}.tar.gz
        URL_HASH SHA256=${ZSTD_TARBALL_SHA256}
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/zstd"
    )

    FetchContent_MakeAvailable(zstd_tarball)
    check_zstd_available(ZSTD_AVAILABLE)
endif()

# Final check
if(NOT ZSTD_AVAILABLE)
    message(FATAL_ERROR "Failed to obtain zstd dependency through all available methods (git submodule, FetchContent+tarball)")
endif()

message(STATUS "zstd dependency resolved successfully")

# Set zstd build options before making it available
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "")
set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "")
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "")

# Add zstd subdirectory directly instead of using FetchContent
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/deps/zstd/build/cmake" zstd_build)
# Note: find_package not needed when using add_subdirectory - targets are directly available
list(APPEND OPENZL_LINK_LIBRARIES libzstd)

find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    list(APPEND OPENZL_LINK_LIBRARIES m)
endif()

# We aren't currently using pthreads, but we expect to, so lets just include it
# now.
# Add it after Zstd because it is incorrectly not linking against Threads
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
set(ZTRONG_HAVE_PTHREAD "${CMAKE_USE_PTHREADS_INIT}")
list(APPEND CMAKE_REQUIRED_LIBRARIES Threads::Threads)
list(APPEND OPENZL_LINK_LIBRARIES Threads::Threads)

add_library(openzl_deps INTERFACE)

list(REMOVE_DUPLICATES OPENZL_INCLUDE_DIRECTORIES)
target_include_directories(openzl_deps INTERFACE ${OPENZL_INCLUDE_DIRECTORIES})
target_link_libraries(openzl_deps INTERFACE
    ${OPENZL_LINK_LIBRARIES}
    ${OPENZL_ASAN_FLAGS}
    ${OPENZL_MSAN_FLAGS}
)
