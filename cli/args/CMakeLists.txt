# Copyright (c) Meta Platforms, Inc. and affiliates.

file(GLOB args_sources
    CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

file(GLOB args_headers
    CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(args ${args_sources})

target_include_directories(args
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_BINARY_DIR}/include
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR})

target_link_libraries(args
    PUBLIC
    openzl_cpp
    custom_parsers
    tools_io
    arg
    utils)


add_dependencies(args
    openzl_cpp
    custom_parsers
    tools_io
    arg
    utils)
