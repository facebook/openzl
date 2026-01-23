# Copyright (c) Meta Platforms, Inc. and affiliates.

set(OPENZL_BUILD_TRAINING_TOOLS OFF)

if(OPENZL_BUILD_TOOLS)
  set(OPENZL_BUILD_TRAINING_TOOLS ON)
endif()
if(OPENZL_BUILD_CLI)
  set(OPENZL_BUILD_TRAINING_TOOLS ON)
endif()

if (OPENZL_BUILD_TRAINING_TOOLS)
  file(
      GLOB_RECURSE training_sources
      CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

  list(FILTER training_sources EXCLUDE REGEX "tests/.*\\.cpp$")

  file(
      GLOB_RECURSE training_headers
      CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
  )

  list(FILTER training_headers EXCLUDE REGEX "tests/.*\\.h$")

  add_library(tools_training
    ${training_sources}
  )
  set_property(TARGET tools_training PROPERTY POSITION_INDEPENDENT_CODE ON)

  target_include_directories(tools_training PUBLIC ${PROJECT_SOURCE_DIR})
  apply_openzl_compile_options_to_target(tools_training)
    target_link_libraries(
        tools_training
        PUBLIC
            openzl
            openzl_cpp
            tools_io
            logger
            ml_selector)
    add_dependencies(
        tools_training
        openzl
        openzl_cpp
        tools_io
        logger
        ml_selector
        xgboost_external)

  if (OPENZL_BUILD_TESTS AND OPENZL_ALLOW_INTROSPECTION)
    file(
        GLOB_RECURSE
        test_training_srcs
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/tests/*.cpp")
    file(
        GLOB_RECURSE
        test_training_headers
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/tests/*.h")
    add_executable(test_training ${test_training_srcs} ${test_training_headers})
    add_dependencies(test_training xgboost_external)
    target_link_libraries(
        test_training
        PRIVATE
            tools_training
            openzl
            openzl_cpp
            custom_parsers
            GTest::gtest_main
    )
    apply_openzl_compile_options_to_target(test_training)
    gtest_discover_tests(test_training)
  endif()
endif()
