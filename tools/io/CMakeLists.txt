# Copyright (c) Meta Platforms, Inc. and affiliates.
if (OPENZL_BUILD_IO_TOOLS)
    file(
        GLOB tools_io_sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

    add_library(tools_io STATIC
        ${tools_io_sources}
    )
    set_property(TARGET tools_io PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_include_directories(tools_io
        PRIVATE ${PROJECT_BINARY_DIR}/include
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR} )
    target_link_libraries(tools_io PUBLIC
        openzl_cpp
        logger
    )
    apply_openzl_compile_options_to_target(tools_io)
    add_dependencies(tools_io
        openzl_cpp
        logger
    )
endif()
