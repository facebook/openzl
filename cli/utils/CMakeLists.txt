# Copyright (c) Meta Platforms, Inc. and affiliates.

# Keep utils sources explicit since GLOB can lead to duplicate
# symbols (it will pull in parse.cpp (owned by cli_util))

set(utils_sources compress_profiles.cpp util.cpp)
set(utils_headers compress_profiles.h util.h)

# Lightweight parsing utilities (no heavy dependencies)
add_library(cli_util parse.cpp)
target_include_directories(cli_util
    PUBLIC ${PROJECT_SOURCE_DIR})
apply_openzl_compile_options_to_target(cli_util)

# Full utils library (heavy dependencies)
# profile_graphs.cpp is built as openzl_profile_graphs and parse.cpp as cli_util,
# so they must not be globbed into utils to avoid duplicate symbols.
add_library(utils compress_profiles.cpp util.cpp)

target_include_directories(utils
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_BINARY_DIR}/include
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR})

target_link_libraries(utils
    PUBLIC
    cli_util
    openzl
    openzl_profile_graphs
    openzl_cpp
    csv_parser
    sddl_profile
    sddl_compiler_lib
    sddl2_compiler_lib
    sddl2_assembler_lib
    pytorch_model_parser
    parquet_graph
    shared_components
    custom_parsers
    tools_io
    logger)

add_dependencies(utils
    openzl
    openzl_cpp
    csv_parser
    sddl_profile
    sddl_compiler_lib
    sddl2_compiler_lib
    sddl2_assembler_lib
    pytorch_model_parser
    parquet_graph
    shared_components
    custom_parsers
    tools_io
    logger
)

apply_openzl_compile_options_to_target(utils)
