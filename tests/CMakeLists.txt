# Copyright (c) Meta Platforms, Inc. and affiliates.

function(add_zs_test TESTNAME)
    add_executable(${ARGV})
    target_link_libraries(${TESTNAME}
        PRIVATE
            openzl_test_support
            datagen
            GTest::gtest_main
    )
    apply_openzl_compile_options_to_target(${TESTNAME})
    gtest_discover_tests(${TESTNAME})
endfunction()

add_subdirectory(datagen)

file(
    GLOB_RECURSE test_fixtures
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/openzl/test_*_fixture.*")

add_library(openzl_test_support
    ${support_sources}
    ${CMAKE_CURRENT_LIST_DIR}/utils.cpp
    ${CMAKE_CURRENT_LIST_DIR}/utils.h
    ${CMAKE_CURRENT_LIST_DIR}/local_params_utils.cpp
    ${CMAKE_CURRENT_LIST_DIR}/local_params_utils.h
    ${CMAKE_CURRENT_LIST_DIR}/ml_selector_utils.cpp
    ${CMAKE_CURRENT_LIST_DIR}/ml_selector_utils.h
    ${test_fixtures}
)
target_include_directories(openzl_test_support
    SYSTEM
    PUBLIC
        ${GTEST_INCLUDE_DIRS}
)
apply_openzl_compile_options_to_target(openzl_test_support)
target_link_libraries(openzl_test_support
    PUBLIC
        openzl
        openzl_cpp
        GTest::gtest)

file(GLOB_RECURSE unittest_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/unittest/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/unittest/*.c")
file(GLOB_RECURSE unittest_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/unittest/*.h")
add_zs_test(unittest ${unittest_srcs} ${unittest_headers})
# list(FILTER lib_srcs EXCLUDE REGEX ".*b44ExpLogTable\\.cpp$")

file(GLOB_RECURSE test_compress_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/compress/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/compress/*.c")
file(GLOB_RECURSE test_compress_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/compress/*.h")
# Exclude sddl2 tests - they have their own build system and test framework (Makefile-based)
list(FILTER test_compress_srcs EXCLUDE REGEX ".*/compress/graphs/sddl2/.*")
list(FILTER test_compress_headers EXCLUDE REGEX ".*/compress/graphs/sddl2/.*")
add_zs_test(test_compress ${test_compress_srcs} ${test_compress_headers})

file(GLOB_RECURSE test_decompress_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/decompress/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/decompress/*.c")
file(GLOB_RECURSE test_decompress_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/decompress/*.h")
add_zs_test(test_decompress ${test_decompress_srcs} ${test_decompress_headers})

if (OPENZL_ALLOW_INTROSPECTION)
    add_zs_test(integrationtest
                ${CMAKE_CURRENT_LIST_DIR}/integrationtest/CompressIntrospectionTest.cpp)
else()
    add_zs_test(integrationtest
                ${CMAKE_CURRENT_LIST_DIR}/integrationtest/NoIntrospectionTest.cpp)
endif()
target_link_libraries(integrationtest PRIVATE openzl_cpp)
add_dependencies(integrationtest openzl_cpp)

file(GLOB_RECURSE test_round_trip_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/*.cpp")
file(GLOB_RECURSE test_round_trip_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/round_trip/*.h")
add_zs_test(test_round_trip ${test_round_trip_srcs} ${test_round_trip_headers})

file(GLOB_RECURSE test_openzl_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/zstrong/test_*.cpp")
file(GLOB_RECURSE test_openzl_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/zstrong/test_*.h")
add_zs_test(test_openzl ${test_openzl_srcs} ${test_openzl_headers})

file(GLOB_RECURSE test_codecs_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/codecs/*.cpp")
file(GLOB_RECURSE test_codecs_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/codecs/*.h")
add_zs_test(test_codecs ${test_codecs_srcs} ${test_codecs_headers})
target_link_libraries(test_codecs
    PRIVATE
        sddl_compiler_lib
)
add_dependencies(test_codecs sddl_compiler_lib)
