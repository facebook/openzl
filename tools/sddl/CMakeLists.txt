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
    file(
        GLOB sddl_compiler_lib_sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/compiler/*.cpp")
    file(
        GLOB sddl_compiler_lib_headers
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/compiler/*.h")
    list(REMOVE_ITEM sddl_compiler_lib_sources
      ${CMAKE_CURRENT_LIST_DIR}/compiler/main.cpp)

    add_library(sddl_compiler_lib
        ${sddl_compiler_lib_sources}
    )
    target_include_directories(sddl_compiler_lib PRIVATE
        ${PROJECT_BINARY_DIR}/include
        ${PROJECT_SOURCE_DIR}
    )
    # This library is depended upon by the python binding wheel, which
    # requires that its components are built with -fPIC. In the future, offer
    # PIC and non-PIC builds?
    set_property(TARGET sddl_compiler_lib PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(sddl_compiler_lib
        openzl_cpp
    )
    add_dependencies(sddl_compiler_lib openzl_cpp)
    apply_openzl_compile_options_to_target(sddl_compiler_lib)


    add_executable(sddl_compiler
        ${CMAKE_CURRENT_LIST_DIR}/compiler/main.cpp
    )
    target_include_directories(sddl_compiler PRIVATE
        ${PROJECT_BINARY_DIR}/include
        ${PROJECT_SOURCE_DIR}
    )
    target_link_libraries(sddl_compiler
        sddl_compiler_lib
    )
    add_dependencies(sddl_compiler sddl_compiler_lib)
    apply_openzl_compile_options_to_target(sddl_compiler)

    if (OPENZL_BUILD_TESTS)
        file(
            GLOB_RECURSE test_sddl_compiler_sources
            CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/compiler/tests/*.cpp")
        add_executable(test_sddl_compiler ${test_sddl_compiler_sources})
        target_link_libraries(test_sddl_compiler
            PRIVATE
                sddl_compiler_lib
                openzl_test_support
                GTest::gtest_main
        )
        add_dependencies(test_sddl_compiler
            sddl_compiler_lib
            openzl_test_support
            GTest::gtest_main)
        apply_openzl_compile_options_to_target(test_sddl_compiler)
        gtest_discover_tests(test_sddl_compiler)
    endif()
endif()
