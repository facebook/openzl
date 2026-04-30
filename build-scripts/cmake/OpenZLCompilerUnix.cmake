# Copyright (c) Meta Platforms, Inc. and affiliates.

set(CMAKE_C_FLAGS_COMMON "-g")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${CMAKE_C_FLAGS_COMMON}")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${CMAKE_C_FLAGS_COMMON} -O3")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} ${CMAKE_C_FLAGS_COMMON} -O3")

set(CMAKE_CXX_FLAGS_COMMON "-g")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_CXX_FLAGS_COMMON}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_CXX_FLAGS_COMMON} -O3")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${CMAKE_CXX_FLAGS_COMMON} -O3")

list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")

set(OPENZL_COMMON_COMPILE_DEFINITIONS
    _REENTRANT
    _GNU_SOURCE)

set(OPENZL_C_COMPILE_DEFINITIONS ${OPENZL_COMMON_COMPILE_DEFINITIONS})
message(DEBUG "OPENZL_C_COMPILE_DEFINITIONS=${OPENZL_C_COMPILE_DEFINITIONS}")
set(OPENZL_CXX_COMPILE_DEFINITIONS ${OPENZL_COMMON_COMPILE_DEFINITIONS})
message(DEBUG "OPENZL_CXX_COMPILE_DEFINITIONS=${OPENZL_CXX_COMPILE_DEFINITIONS}")

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)

check_c_compiler_flag(-Wgnu-zero-variadic-macro-arguments OPENZL_HAVE_C_GNU_ZERO_VARIADIC_MACRO_ARGUMENTS)
check_c_compiler_flag(-Wvariadic-macros OPENZL_HAVE_C_VARIADIC_MACROS)
check_cxx_compiler_flag(-Wgnu-zero-variadic-macro-arguments OPENZL_HAVE_CXX_GNU_ZERO_VARIADIC_MACRO_ARGUMENTS)
check_cxx_compiler_flag(-Wvariadic-macros OPENZL_HAVE_CXX_VARIADIC_MACROS)

if (${OPENZL_HAVE_C_GNU_ZERO_VARIADIC_MACRO_ARGUMENTS})
    set(OPENZL_C_VARIADIC_MACROS_OPTIONS -Wgnu-zero-variadic-macro-arguments)
elseif (${OPENZL_HAVE_C_VARIADIC_MACROS})
    set(OPENZL_C_VARIADIC_MACROS_OPTIONS -Wvariadic-macros)
endif()

if (${OPENZL_HAVE_CXX_GNU_ZERO_VARIADIC_MACRO_ARGUMENTS})
    set(OPENZL_CXX_VARIADIC_MACROS_OPTIONS -Wgnu-zero-variadic-macro-arguments)
elseif (${OPENZL_HAVE_CXX_VARIADIC_MACROS})
    set(OPENZL_CXX_VARIADIC_MACROS_OPTIONS -Wvariadic-macros)
endif()

separate_arguments(OPENZL_COMMON_COMPILE_OPTIONS UNIX_COMMAND "${OPENZL_COMMON_FLAGS}")
set(OPENZL_COMMON_COMPILE_OPTIONS
    -g
    ${OPENZL_COMMON_COMPILE_OPTIONS})



separate_arguments(OPENZL_C_COMPILE_OPTIONS UNIX_COMMAND "${OPENZL_C_FLAGS}")
set(OPENZL_C_COMPILE_OPTIONS
    ${OPENZL_COMMON_COMPILE_OPTIONS}
    ${OPENZL_C_COMPILE_OPTIONS})
message(DEBUG "OPENZL_C_COMPILE_OPTIONS=${OPENZL_C_COMPILE_OPTIONS}")

separate_arguments(OPENZL_CXX_COMPILE_OPTIONS UNIX_COMMAND "${OPENZL_CXX_FLAGS}")
set(OPENZL_CXX_COMPILE_OPTIONS
    ${OPENZL_COMMON_COMPILE_OPTIONS}
    ${OPENZL_CXX_COMPILE_OPTIONS})
message(DEBUG "OPENZL_CXX_COMPILE_OPTIONS=${OPENZL_CXX_COMPILE_OPTIONS}")

set(OPENZL_COMMON_COMPILE_WARNINGS
    -Wall
    -Wcast-qual
    -Wcast-align
    -Wshadow
    -Wstrict-aliasing=1
    -Wundef
    -Wpointer-arith
    -Wvla
    -Wformat=2
    -Wfloat-equal
    -Wswitch-enum
    -Wimplicit-fallthrough
    -Wno-unused-function)
message(DEBUG "OPENZL_COMMON_COMPILE_WARNINGS=${OPENZL_COMMON_COMPILE_WARNINGS}")

set(OPENZL_C_COMPILE_WARNINGS
    -Wextra
    -Wno-missing-field-initializers
    -Wconversion
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wredundant-decls
    ${OPENZL_C_VARIADIC_MACROS_OPTIONS}
    ${OPENZL_COMMON_COMPILE_WARNINGS})
message(DEBUG "OPENZL_C_COMPILE_WARNINGS=${OPENZL_C_COMPILE_WARNINGS}")

set(OPENZL_CXX_COMPILE_WARNINGS
    ${OPENZL_CXX_VARIADIC_MACROS_OPTIONS}
    ${OPENZL_COMMON_COMPILE_WARNINGS})
message(DEBUG "OPENZL_CXX_COMPILE_WARNINGS=${OPENZL_CXX_COMPILE_WARNINGS}")


set_property(
    SOURCE
    ${OPENZL_SRC_DIR}/openzl/common/errors.c
    APPEND PROPERTY COMPILE_OPTIONS -Wno-format-nonliteral)
set_property(
    SOURCE
    ${OPENZL_SRC_DIR}/openzl/common/logging.c
    APPEND PROPERTY COMPILE_OPTIONS -Wno-format-nonliteral)
set_property(
    SOURCE
    ${OPENZL_SRC_DIR}/openzl/common/errors.c
    APPEND PROPERTY COMPILE_OPTIONS -Wno-format-nonliteral)
set_property(
    SOURCE
    ${OPENZL_SRC_DIR}/openzl/common/logging.c
    APPEND PROPERTY COMPILE_OPTIONS -Wno-format-nonliteral)

function(apply_openzl_compile_options_to_target THETARGET)
    set(options NO_WARNINGS)
    set(oneValueArgs)
    set(multiValueArgs)
    cmake_parse_arguments(PARSE_ARGV 1 arg "${options}" "${oneValueArgs}" "${multiValueArgs}")

    # Warnings first so that the user can override them
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

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    if ("${CMAKE_CXX_COMPILER_VERSION}" VERSION_LESS "9.0")
        # For gcc versions < 9 link against libstdc++fs when we need it
        set(OPENZL_FILESYSTEM_LIBRARY stdc++fs)
    endif()
endif()
