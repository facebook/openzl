# Copyright (c) Meta Platforms, Inc. and affiliates.

if (OPENZL_BUILD_LOGGER)

add_library(logger
    Logger.cpp
)
set_property(TARGET logger PROPERTY POSITION_INDEPENDENT_CODE ON)

target_include_directories(logger PUBLIC ${PROJECT_SOURCE_DIR})
apply_openzl_compile_options_to_target(logger)

if (OPENZL_BUILD_TESTS)
    add_executable(test_logger
        ${CMAKE_CURRENT_LIST_DIR}/tests/LoggerTest.cpp)
    target_link_libraries(test_logger
        PRIVATE
            logger
            GTest::gtest_main
    )
    apply_openzl_compile_options_to_target(test_logger)
    gtest_discover_tests(test_logger)
endif()
endif()
