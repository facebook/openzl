# Copyright (c) Meta Platforms, Inc. and affiliates.

file(
    GLOB onnx_sources
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.c")
file(
    GLOB onnx_parser_headers
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.h")

add_library(onnx_parser ${onnx_sources})
target_include_directories(onnx_parser PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR})
target_link_libraries(onnx_parser
    openzl
    shared_components
)

add_dependencies(onnx_parser openzl)
apply_openzl_compile_options_to_target(onnx_parser)
