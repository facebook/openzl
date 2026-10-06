# Copyright (c) Meta Platforms, Inc. and affiliates.

# Examples for openzl library

# Define the example_utils library
add_library(example_utils
    example_utils.cpp
    example_utils.h
)
target_link_libraries(example_utils
    PUBLIC
        openzl
        fileio
)
add_dependencies(example_utils openzl fileio)
target_include_directories(example_utils
    PUBLIC
        ${PROJECT_SOURCE_DIR}
)

# Define the compress executable
add_executable(compress
    compress_app.cpp
)
target_link_libraries(compress
    PRIVATE
        openzl
)
add_dependencies(compress openzl)

# Define the zs2_pipeline executable
add_executable(zs2_pipeline
    zs2_pipeline.c
)
target_link_libraries(zs2_pipeline
    PRIVATE
        openzl
        fileio
)
add_dependencies(zs2_pipeline openzl fileio)

# Define the zs2_trygraph executable
add_executable(zs2_trygraph
    zs2_trygraph.c
)
target_link_libraries(zs2_trygraph
    PRIVATE
        openzl
        fileio
)
add_dependencies(zs2_trygraph openzl fileio)

# Define the zs2_selector executable
add_executable(zs2_selector
    zs2_selector.c
)
target_link_libraries(zs2_selector
    PRIVATE
        openzl
        fileio
)
add_dependencies(zs2_selector openzl fileio)

# Define the numeric_array executable
add_executable(numeric_array
    numeric_array.cpp
)
target_link_libraries(numeric_array
    PRIVATE
        example_utils
        openzl
)
add_dependencies(numeric_array example_utils openzl)

# Define the training executable
add_executable(training
    training.cpp
)
target_link_libraries(training
    PRIVATE
        example_utils
        openzl
        openzl_cpp
        fileio
        tools_io
        tools_training
        shared_components
)
add_dependencies(
    training
    example_utils
    openzl
    openzl_cpp
    fileio
    tools_io
    tools_training
    shared_components
)

# BinaryFILE ITCH parser: only standard nodes, no custom decoder.
add_executable(itch itch.cpp)
target_link_libraries(itch PRIVATE openzl)
if(OPENZL_BUILD_TESTS)
    add_test(NAME itch_example_round_trip COMMAND itch --self-test)
endif()
