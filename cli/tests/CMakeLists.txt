if(NOT COMMAND add_zs_test)
  function(add_zs_test TESTNAME)
      add_executable(${ARGV})
      target_link_libraries(${TESTNAME}
          PRIVATE
              GTest::gtest_main
      )
      gtest_discover_tests(${TESTNAME})
  endfunction()
endif()

enable_testing()

if (BUILD_TESTS)
    add_zs_test(cli_util_test unittest/UtilTest.cpp)
    target_link_libraries(cli_util_test PRIVATE utils)
    target_include_directories(cli_util_test PRIVATE
        ${PROJECT_BINARY_DIR}/include
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR})
    apply_openzl_compile_options_to_target(cli_util_test)
endif()
