# Copyright (c) Meta Platforms, Inc. and affiliates.

# MSVC and clang-cl compiler configuration for OpenZL

# Detect ClangCL toolset (the standard way)
set(USING_CLANG_CL FALSE)
if (CMAKE_GENERATOR_TOOLSET STREQUAL "ClangCL")
    set(USING_CLANG_CL TRUE)
    message(STATUS "Using ClangCL toolset")
else()
    message(STATUS "Using MSVC toolset")
endif()

# Define compile definitions for MSVC
set(OPENZL_COMMON_COMPILE_DEFINITIONS
    _REENTRANT
    WIN32_LEAN_AND_MEAN
    NOMINMAX)

set(OPENZL_C_COMPILE_DEFINITIONS ${OPENZL_COMMON_COMPILE_DEFINITIONS})
set(OPENZL_CXX_COMPILE_DEFINITIONS ${OPENZL_COMMON_COMPILE_DEFINITIONS})

# Define compile options for MSVC
set(OPENZL_COMMON_COMPILE_OPTIONS)
set(OPENZL_C_COMPILE_OPTIONS ${OPENZL_COMMON_COMPILE_OPTIONS})
set(OPENZL_CXX_COMPILE_OPTIONS ${OPENZL_COMMON_COMPILE_OPTIONS})

# Define warning options for MSVC
set(OPENZL_COMMON_COMPILE_WARNINGS)
set(OPENZL_C_COMPILE_WARNINGS ${OPENZL_COMMON_COMPILE_WARNINGS})
set(OPENZL_CXX_COMPILE_WARNINGS ${OPENZL_COMMON_COMPILE_WARNINGS})

# Add any additional flags from OPENZL_COMMON_FLAGS
if(DEFINED OPENZL_COMMON_FLAGS)
    separate_arguments(OPENZL_COMMON_FLAGS_LIST WINDOWS_COMMAND "${OPENZL_COMMON_FLAGS}")
    list(APPEND OPENZL_COMMON_COMPILE_OPTIONS ${OPENZL_COMMON_FLAGS_LIST})
    list(APPEND OPENZL_C_COMPILE_OPTIONS ${OPENZL_COMMON_FLAGS_LIST})
    list(APPEND OPENZL_CXX_COMPILE_OPTIONS ${OPENZL_COMMON_FLAGS_LIST})
endif()

# Function to apply OpenZL compile options to a target (required by CMakeLists.txt)
function(apply_openzl_compile_options_to_target THETARGET)
    set(options NO_WARNINGS)
    set(oneValueArgs)
    set(multiValueArgs)
    cmake_parse_arguments(PARSE_ARGV 1 arg "${options}" "${oneValueArgs}" "${multiValueArgs}")

    # Apply warnings first so that the user can override them
    if (NOT ${arg_NO_WARNINGS})
        target_compile_options(${THETARGET}
            PRIVATE
                $<$<COMPILE_LANGUAGE:C>:${OPENZL_C_COMPILE_WARNINGS}>
                $<$<COMPILE_LANGUAGE:CXX>:${OPENZL_CXX_COMPILE_WARNINGS}>
        )
    endif()

    target_compile_definitions(${THETARGET}
        PRIVATE
            $<$<COMPILE_LANGUAGE:C>:${OPENZL_C_COMPILE_DEFINITIONS}>
            $<$<COMPILE_LANGUAGE:CXX>:${OPENZL_CXX_COMPILE_DEFINITIONS}>
    )
    target_compile_options(${THETARGET}
        PRIVATE
            $<$<COMPILE_LANGUAGE:C>:${OPENZL_C_COMPILE_OPTIONS}>
            $<$<COMPILE_LANGUAGE:CXX>:${OPENZL_CXX_COMPILE_OPTIONS}>
    )
endfunction()

message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "Using clang-cl: ${USING_CLANG_CL}")
message(STATUS "Generator toolset: ${CMAKE_GENERATOR_TOOLSET}")
