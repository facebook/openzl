# Copyright (c) Meta Platforms, Inc. and affiliates.

add_library(ml_selector_graph ml_selector_graph.c ml_selector_graph.h)
target_include_directories(ml_selector_graph PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(ml_selector_graph PUBLIC openzl)

if (OPENZL_BUILD_TESTS)
    add_executable(test_ml_selector_graph test_mlSelectorGraph.cpp)
    target_link_libraries(test_ml_selector_graph
        PRIVATE
            ml_selector_graph
            openzl
            openzl_cpp
            openzl_test_support
            GTest::gtest_main
    )
    apply_openzl_compile_options_to_target(test_ml_selector_graph)
    gtest_discover_tests(test_ml_selector_graph)
endif()
