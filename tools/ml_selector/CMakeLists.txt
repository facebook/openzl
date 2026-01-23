# Copyright (c) Meta Platforms, Inc. and affiliates.

include(ExternalProject)
include(GNUInstallDirs)

# Build xgboost as an external project
set(XGBOOST_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/xgboost-install")
set(XGBOOST_LIB_DIR "${XGBOOST_INSTALL_DIR}/lib")

# Create install directories at configure time
file(MAKE_DIRECTORY ${XGBOOST_INSTALL_DIR}/include)
file(MAKE_DIRECTORY ${XGBOOST_LIB_DIR})

# Add XGBoost as external project due to vs build issues
# Turn logs on to prevent polluting stdout when building xgboost
# Disable DMLC stack trace to avoid dependency on execinfo.h
ExternalProject_Add(xgboost_external
    GIT_REPOSITORY https://github.com/dmlc/xgboost.git
    GIT_TAG        ccb511768e13d1670c10be07dea89d0edca138f3 # v3.1.0
    GIT_SUBMODULES "dmlc-core"
    GIT_SHALLOW    TRUE
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${XGBOOST_INSTALL_DIR}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DUSE_OPENMP=OFF
        -DBUILD_TESTING=OFF
        -DUSE_CUDA=OFF
        -DUSE_NCCL=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_STATIC_LIB=ON
        "-DCMAKE_C_FLAGS=-DDMLC_LOG_STACK_TRACE=0"
        "-DCMAKE_CXX_FLAGS=-DDMLC_LOG_STACK_TRACE=0"
        ${XGBOOST_EXTRA_CMAKE_ARGS}
    BUILD_BYPRODUCTS
        ${XGBOOST_LIB_DIR}/libxgboost${CMAKE_STATIC_LIBRARY_SUFFIX}
        ${XGBOOST_LIB_DIR}/libdmlc${CMAKE_STATIC_LIBRARY_SUFFIX}
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
)

# Create imported targets
add_library(xgboost STATIC IMPORTED GLOBAL)
set_target_properties(xgboost PROPERTIES
    IMPORTED_LOCATION
        ${XGBOOST_LIB_DIR}/libxgboost${CMAKE_STATIC_LIBRARY_SUFFIX}
    INTERFACE_INCLUDE_DIRECTORIES ${XGBOOST_INSTALL_DIR}/include
)
add_dependencies(xgboost xgboost_external)

if(WIN32 OR MINGW)
    set_target_properties(xgboost PROPERTIES
        INTERFACE_LINK_LIBRARIES "ws2_32")
endif()

add_library(dmlc STATIC IMPORTED GLOBAL)
set_target_properties(dmlc PROPERTIES
    IMPORTED_LOCATION ${XGBOOST_LIB_DIR}/libdmlc${CMAKE_STATIC_LIBRARY_SUFFIX}
    INTERFACE_INCLUDE_DIRECTORIES ${XGBOOST_INSTALL_DIR}/include
)
add_dependencies(dmlc xgboost_external)

add_library(ml_selector
    ml_selector_trainer.cpp
    ml_selector_trainer.h
    ml_features.cpp
    ml_features.h
)

target_include_directories(ml_selector
    PUBLIC
        ${PROJECT_SOURCE_DIR}
        ${XGBOOST_INSTALL_DIR}/include
)

# Disable DMLC stack trace to avoid dependency on execinfo.h
target_compile_definitions(ml_selector
    PUBLIC
        DMLC_LOG_STACK_TRACE=0
)

target_link_libraries(ml_selector
        PUBLIC
        openzl
        openzl_cpp
        xgboost
        dmlc
    )

add_dependencies(ml_selector xgboost_external)

if (OPENZL_BUILD_TESTS)
    add_executable(test_ml_selector tests/test_mlSelectorGraph.cpp)
    add_dependencies(test_ml_selector xgboost_external)

    target_link_libraries(test_ml_selector
        PRIVATE
            ml_selector
            openzl
            openzl_cpp
            openzl_test_support
            tools_training
            GTest::gtest_main
    )
    apply_openzl_compile_options_to_target(test_ml_selector)

    gtest_discover_tests(test_ml_selector)

    if (OPENZL_ALLOW_INTROSPECTION)
        add_executable(test_ml_selector_trainer
            tests/test_mlSelectorTrainer.cpp)
        add_dependencies(test_ml_selector_trainer xgboost_external)

        target_link_libraries(test_ml_selector_trainer
            PRIVATE
                ml_selector
                openzl
                openzl_cpp
                openzl_test_support
                tools_training
                GTest::gtest_main
        )
        apply_openzl_compile_options_to_target(test_ml_selector_trainer)
        gtest_discover_tests(test_ml_selector_trainer)
    endif()
endif()
