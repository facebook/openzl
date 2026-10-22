# Copyright (c) Meta Platforms, Inc. and affiliates.

file(
    GLOB gguf_sources
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.c")
file(
    GLOB gguf_parser_headers
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.h")

add_library(gguf_parser ${gguf_sources})
target_include_directories(gguf_parser PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR})
target_link_libraries(gguf_parser
    openzl
    shared_components
)

add_dependencies(gguf_parser openzl)
apply_openzl_compile_options_to_target(gguf_parser)
