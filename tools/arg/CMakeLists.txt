# Copyright (c) Meta Platforms, Inc. and affiliates.

if (OPENZL_BUILD_ARG_TOOLS)
    file(
        GLOB_RECURSE arg_sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
    file(
        GLOB_RECURSE arg_headers
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

    list(FILTER arg_sources EXCLUDE REGEX ".*/tests/.*")

    add_library(arg
      ${arg_sources}
    )
    target_include_directories(arg PUBLIC ${PROJECT_SOURCE_DIR})
    apply_openzl_compile_options_to_target(arg)

    if (OPENZL_BUILD_TESTS)
        add_executable(test_arg_parser
            ${CMAKE_CURRENT_LIST_DIR}/tests/arg_parser_test.cpp)
        target_link_libraries(test_arg_parser
            PRIVATE
                arg
                GTest::gtest_main
        )
        apply_openzl_compile_options_to_target(test_arg_parser)
        gtest_discover_tests(test_arg_parser)
    endif()
endif()
