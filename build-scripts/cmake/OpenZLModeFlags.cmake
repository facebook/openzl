# Copyright (c) Meta Platforms, Inc. and affiliates.

set(availableModes none dev dev-nosan opt opt-asan dbgo dbgo-asan)
set(OPENZL_BUILD_MODE none CACHE STRING "Build @mode chosen by the user at CMake configure time")
set_property(CACHE OPENZL_BUILD_MODE PROPERTY STRINGS ${availableModes})

# ensure the user enters a valid build mode or leaves it to the default
if(NOT OPENZL_BUILD_MODE IN_LIST availableModes)
    message(FATAL_ERROR "OPENZL_BUILD_MODE must be one of ${availableModes}")
endif()
message(STATUS "Build mode: ${OPENZL_BUILD_MODE}")

# apply build flags globally
if (OPENZL_BUILD_MODE STREQUAL "dev")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fsanitize=address,undefined -O0 -g")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address,undefined -O0 -g")
    set(OPENZL_SANITIZE_ADDRESS ON) # is this necessary? this does the same thing in openzl-deps
    set(CMAKE_BUILD_TYPE Debug)
elseif (OPENZL_BUILD_MODE STREQUAL "dev-nosan")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")
    set(CMAKE_BUILD_TYPE Debug)
elseif (OPENZL_BUILD_MODE STREQUAL "opt")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -DNDEBUG")
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
elseif (OPENZL_BUILD_MODE STREQUAL "opt-asan")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -DNDEBUG")
    set(OPENZL_SANITIZE_ADDRESS ON) # is this necessary? this does the same thing in openzl-deps
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
elseif (OPENZL_BUILD_MODE STREQUAL "dbgo")
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
elseif (OPENZL_BUILD_MODE STREQUAL "dbgo-asan")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -fsanitize=address,undefined")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -fsanitize=address,undefined")
    set(OPENZL_SANITIZE_ADDRESS ON) # is this necessary? this does the same thing in openzl-deps
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
