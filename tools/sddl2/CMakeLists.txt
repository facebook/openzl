# Copyright (c) Meta Platforms, Inc. and affiliates.

set(OPENZL_BUILD_SDDL_TOOLS OFF)

if(OPENZL_BUILD_TOOLS)
  set(OPENZL_BUILD_SDDL_TOOLS ON)
endif()
if(OPENZL_BUILD_CLI)
  set(OPENZL_BUILD_SDDL_TOOLS ON)
endif()
if(OPENZL_BUILD_PYTHON_EXT)
  set(OPENZL_BUILD_SDDL_TOOLS ON)
endif()

if (OPENZL_BUILD_SDDL_TOOLS)
    add_executable(sddl2_compiler
        ${CMAKE_CURRENT_LIST_DIR}/compiler/main.cpp
    )
    target_include_directories(sddl2_compiler PUBLIC ${PROJECT_SOURCE_DIR})
    apply_openzl_compile_options_to_target(sddl2_compiler)
    target_link_libraries(sddl2_compiler
        PRIVATE
            sddl2_compiler_lib
    )
    add_dependencies(sddl2_compiler sddl2_compiler_lib)

    file(
        GLOB sddl2_compiler_lib_sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/compiler/*.cpp")
    list(REMOVE_ITEM sddl2_compiler_lib_sources
      ${CMAKE_CURRENT_SOURCE_DIR}/compiler/main.cpp)
    add_library(sddl2_compiler_lib
        ${sddl2_compiler_lib_sources}
    )
    target_include_directories(sddl2_compiler_lib
        PUBLIC
            ${PROJECT_SOURCE_DIR})
    # This library is depended upon by the python binding wheel, which
    # requires that its components are built with -fPIC. In the future, offer
    # PIC and non-PIC builds?
    set_property(TARGET sddl2_compiler_lib PROPERTY POSITION_INDEPENDENT_CODE ON)
    apply_openzl_compile_options_to_target(sddl2_compiler_lib)
    target_link_libraries(sddl2_compiler_lib
        PUBLIC
            openzl_cpp)
    add_dependencies(sddl2_compiler_lib
        openzl_cpp)

    if (OPENZL_BUILD_TESTS)
        file(
            GLOB_RECURSE test_sddl2_compiler_sources
            CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/compiler/tests/*.cpp")
        add_executable(test_sddl2_compiler ${test_sddl2_compiler_sources})
        target_link_libraries(test_sddl2_compiler
            PRIVATE
                sddl2_compiler_lib
                openzl_test_support
                GTest::gtest_main
        )
        add_dependencies(test_sddl2_compiler
            sddl2_compiler_lib
            openzl_test_support
            GTest::gtest_main)
        apply_openzl_compile_options_to_target(test_sddl2_compiler)
        gtest_discover_tests(test_sddl2_compiler)
    endif()
endif()
