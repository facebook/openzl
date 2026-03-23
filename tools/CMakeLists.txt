# Copyright (c) Meta Platforms, Inc. and affiliates.

# ── Flag computations ────────────────────────────────────────────────────────
# Computed here (parent scope) so that all flags are available before any
# add_subdirectory() call, regardless of inclusion order.

set(OPENZL_BUILD_ARG_TOOLS OFF)
set(OPENZL_BUILD_IO_TOOLS OFF)
set(OPENZL_BUILD_SDDL_TOOLS OFF)
set(OPENZL_BUILD_TRAINING_TOOLS OFF)
set(OPENZL_BUILD_ML_SELECTOR OFF)

if(NOT DEFINED OPENZL_BUILD_LOGGER)
  set(OPENZL_BUILD_LOGGER OFF)
endif()

if(OPENZL_BUILD_TOOLS)
  set(OPENZL_BUILD_ARG_TOOLS ON)
  set(OPENZL_BUILD_IO_TOOLS ON)
  set(OPENZL_BUILD_SDDL_TOOLS ON)
  set(OPENZL_BUILD_TRAINING_TOOLS ON)
  set(OPENZL_BUILD_ML_SELECTOR ON)
endif()
if(OPENZL_BUILD_CLI)
  set(OPENZL_BUILD_ARG_TOOLS ON)
  set(OPENZL_BUILD_IO_TOOLS ON)
  set(OPENZL_BUILD_LOGGER ON)
  set(OPENZL_BUILD_SDDL_TOOLS ON)
  set(OPENZL_BUILD_TRAINING_TOOLS ON)
  set(OPENZL_BUILD_ML_SELECTOR ON)
endif()
if(OPENZL_BUILD_PYTHON_EXT)
  set(OPENZL_BUILD_LOGGER ON)
  set(OPENZL_BUILD_IO_TOOLS ON)
  set(OPENZL_BUILD_SDDL_TOOLS ON)
endif()

# Common Utilities
add_subdirectory(arg)
add_subdirectory(fileio)
add_subdirectory(io)
add_subdirectory(logger)
add_subdirectory(time)

# Tools
add_subdirectory(ml_selector)
add_subdirectory(parquet)
add_subdirectory(sddl)
add_subdirectory(sddl2)
add_subdirectory(streamdump)
add_subdirectory(training)
add_subdirectory(protobuf)
