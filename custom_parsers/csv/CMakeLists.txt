# Copyright (c) Meta Platforms, Inc. and affiliates.

file(
    GLOB csv_sources
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.c")
list(FILTER csv_sources EXCLUDE REGEX "${CMAKE_CURRENT_LIST_DIR}/tests/.*$")
file(
    GLOB csv_parser_headers
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.h")

add_library(csv_parser ${csv_sources})
target_include_directories(csv_parser PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR})
target_link_libraries(csv_parser
    openzl
    shared_components
)

add_dependencies(csv_parser openzl shared_components)
apply_openzl_compile_options_to_target(csv_parser)
