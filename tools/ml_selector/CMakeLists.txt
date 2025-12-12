# Copyright (c) Meta Platforms, Inc. and affiliates.

add_library(ml_selector_graph
    ml_selector_graph.c
    ml_selector_graph.h
    ml_selector_trainer.cpp
    ml_selector_trainer.h
    ml_features.cpp
    ml_features.h
)
target_include_directories(ml_selector_graph PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(ml_selector_graph
    PUBLIC
        openzl
        openzl_cpp
    )

if (OPENZL_BUILD_TESTS)
    add_executable(test_ml_selector_graph tests/test_mlSelectorGraph.cpp)
    add_executable(test_ml_selector_trainer tests/test_mlSelectorTrainer.cpp)
    target_link_libraries(test_ml_selector_graph
        PRIVATE
            ml_selector_graph
            openzl
            openzl_cpp
            openzl_test_support
            tools_training
            GTest::gtest_main
    )
    target_link_libraries(test_ml_selector_trainer
        PRIVATE
            ml_selector_graph
            openzl
            openzl_cpp
            openzl_test_support
            tools_training
            GTest::gtest_main
    )
    apply_openzl_compile_options_to_target(test_ml_selector_graph)
    apply_openzl_compile_options_to_target(test_ml_selector_trainer)
    gtest_discover_tests(test_ml_selector_graph)
    gtest_discover_tests(test_ml_selector_trainer)
endif()
