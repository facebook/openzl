# Copyright (c) Meta Platforms, Inc. and affiliates.

if (OPENZL_BUILD_SDDL_TOOLS)
    # Compiler
    file(
        GLOB_RECURSE sddl2_compiler_lib_sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/compiler/*.cpp")
    list(FILTER sddl2_compiler_lib_sources EXCLUDE REGEX
        ".*/compiler/tests/.*")

    file(
        GLOB_RECURSE sddl2_compiler_lib_headers
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/compiler/*.h")
    list(FILTER sddl2_compiler_lib_headers EXCLUDE REGEX
        ".*/compiler/tests/.*")
    list(REMOVE_ITEM sddl2_compiler_lib_sources
      ${CMAKE_CURRENT_LIST_DIR}/compiler/main.cpp)

    add_library(sddl2_compiler_lib
        ${sddl2_compiler_lib_sources}
    )
    target_include_directories(sddl2_compiler_lib PRIVATE
        ${PROJECT_BINARY_DIR}/include
        ${PROJECT_SOURCE_DIR}
    )
    # This library is depended upon by the python binding wheel, which
    # requires that its components are built with -fPIC. In the future, offer
    # PIC and non-PIC builds?
    set_property(TARGET sddl2_compiler_lib PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(sddl2_compiler_lib
        openzl_cpp
    )
    add_dependencies(sddl2_compiler_lib
        openzl_cpp
    )
    apply_openzl_compile_options_to_target(sddl2_compiler_lib)


    add_executable(sddl2_compiler
        ${CMAKE_CURRENT_LIST_DIR}/compiler/main.cpp
    )
    target_include_directories(sddl2_compiler PRIVATE
        ${PROJECT_BINARY_DIR}/include
        ${PROJECT_SOURCE_DIR}
    )
    target_link_libraries(sddl2_compiler PRIVATE
        sddl2_compiler_lib
        tools_io
        openzl_cpp
    )
    add_dependencies(sddl2_compiler
        sddl2_compiler_lib
        tools_io
        openzl_cpp
    )
    apply_openzl_compile_options_to_target(sddl2_compiler)

    # Assembler
    file(
        GLOB sddl2_assembler_lib_sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/assembler/*.cpp")
    list(REMOVE_ITEM sddl2_assembler_lib_sources
      ${CMAKE_CURRENT_LIST_DIR}/assembler/main.cpp)
    file(
        GLOB sddl2_assembler_lib_headers
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/assembler/*.h")

    add_library(sddl2_assembler_lib
        ${sddl2_assembler_lib_sources}
    )
    target_include_directories(sddl2_assembler_lib PRIVATE
        ${PROJECT_BINARY_DIR}/include
        ${PROJECT_SOURCE_DIR}
    )
    target_link_libraries(sddl2_assembler_lib
        openzl_cpp
    )
    apply_openzl_compile_options_to_target(sddl2_assembler_lib)

    add_executable(sddl2_assembler
        ${CMAKE_CURRENT_LIST_DIR}/assembler/main.cpp
    )
    target_link_libraries(sddl2_assembler
        sddl2_assembler_lib
        tools_io
    )
    add_dependencies(sddl2_assembler sddl2_assembler_lib)
    apply_openzl_compile_options_to_target(sddl2_assembler)


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
