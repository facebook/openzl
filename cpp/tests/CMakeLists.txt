if (OPENZL_BUILD_TESTS)
    file(
        GLOB_RECURSE
        cpp_tests
        CONFIGURE_DEPENDS
            "${PROJECT_SOURCE_DIR}/cpp/tests/*.cpp"
    )
    add_executable(test_openzl_cpp ${cpp_tests})
    target_link_libraries(
        test_openzl_cpp
        PRIVATE
            openzl_cpp
            datagen
            GTest::gtest_main
    )
    apply_openzl_compile_options_to_target(test_openzl_cpp)
    gtest_discover_tests(test_openzl_cpp)
endif()
