if (OPENZL_BUILD_PROTOBUF_TOOLS)
    cmake_minimum_required(VERSION 3.24)
    # Configure protobuf options before fetching
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(protobuf_WITH_ZLIB_DEFAULT OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    # Get the protobuf library
    FetchContent_Declare(protobuf
        GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
        GIT_TAG        v32.0
        FIND_PACKAGE_ARGS NAMES protobuf
    )
    FetchContent_MakeAvailable(protobuf)
    # Only set compile options if libprotobuf target exists and is built by this project
    if(TARGET libprotobuf AND NOT TARGET protobuf::libprotobuf)
        target_compile_options(libprotobuf PUBLIC
            -Wno-deprecated-declarations
            -Wno-stringop-overread
            -Wno-strict-aliasing
            -Wno-attributes
        )
    endif()
    find_package(protobuf CONFIG REQUIRED)

    # Make the protobuf_serializer_dynamic library
    file(GLOB protobuf_serializer_dynamic_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    )
    list(REMOVE_ITEM protobuf_serializer_dynamic_sources "${CMAKE_CURRENT_SOURCE_DIR}/cli_dynamic.cpp")

    add_library(protobuf_serializer_dynamic ${protobuf_serializer_dynamic_sources})

    target_link_libraries(protobuf_serializer_dynamic PUBLIC
        openzl_cpp
        protobuf::libprotobuf
    )
    apply_openzl_compile_options_to_target(protobuf_serializer_dynamic)

    # Make the protobuf_cli_dynamic tool
    file(GLOB protobuf_cli_dynamic_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/cli_dynamic.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/schema.proto")
    add_executable(protobuf_cli_dynamic ${protobuf_cli_dynamic_sources})

    include(FindProtobuf)
    protobuf_generate(TARGET protobuf_cli_dynamic)

    target_link_libraries(protobuf_cli_dynamic PRIVATE
        protobuf_serializer_dynamic
        arg
        tools_io
        tools_training
    )
    target_include_directories(protobuf_cli_dynamic PRIVATE ${PROJECT_BINARY_DIR})

    apply_openzl_compile_options_to_target(protobuf_cli_dynamic)
endif()