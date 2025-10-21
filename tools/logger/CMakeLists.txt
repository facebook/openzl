# Copyright (c) Meta Platforms, Inc. and affiliates.

if(NOT DEFINED OPENZL_BUILD_LOGGER)
  set(OPENZL_BUILD_LOGGER OFF)
endif()

if(OPENZL_BUILD_CLI)
  set(OPENZL_BUILD_LOGGER ON)
endif()

if (OPENZL_BUILD_LOGGER)

add_library(logger
    Logger.cpp
)
set_property(TARGET logger PROPERTY POSITION_INDEPENDENT_CODE ON)

target_include_directories(logger PUBLIC ${PROJECT_SOURCE_DIR})
apply_openzl_compile_options_to_target(logger)
endif()
