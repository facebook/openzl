# Copyright (c) Meta Platforms, Inc. and affiliates.

file(
    GLOB safetensors_sources
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.c")
file(
    GLOB safetensors_parser_headers
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.h")

add_library(safetensors_parser ${safetensors_sources})
target_include_directories(safetensors_parser PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR})
target_link_libraries(safetensors_parser
    openzl
)

add_dependencies(safetensors_parser openzl)
apply_openzl_compile_options_to_target(safetensors_parser)
