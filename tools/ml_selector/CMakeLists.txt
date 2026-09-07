# Copyright (c) Meta Platforms, Inc. and affiliates.
if(OPENZL_BUILD_ML_SELECTOR)
    # Prefer a pre-installed xgboost (e.g. from conda/pixi) when one is
    # available, and only fall back to building it from source otherwise. The
    # CONDA_PREFIX hint lets `pixi run` environments be discovered without any
    # extra -DCMAKE_PREFIX_PATH flag.
    find_package(xgboost CONFIG QUIET HINTS "$ENV{CONDA_PREFIX}")

    if(xgboost_FOUND)
        message(STATUS "Using pre-installed xgboost: ${xgboost_DIR}")
        # The imported xgboost::xgboost target carries its own include dirs
        # (covering both xgboost/ and dmlc/ headers) and statically bundles dmlc,
        # so no separate dmlc link target or include path is required.
        set(OPENZL_XGBOOST_LIBS xgboost::xgboost)
    else()
        message(STATUS
            "Pre-installed xgboost not found; building from source via ExternalProject")
        include(ExternalProject)
        include(GNUInstallDirs)

        # Build xgboost as an external project
        set(XGBOOST_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/xgboost-install")
        set(XGBOOST_LIB_DIR "${XGBOOST_INSTALL_DIR}/lib")

        # Create install directories at configure time
        file(MAKE_DIRECTORY ${XGBOOST_INSTALL_DIR}/include)
        file(MAKE_DIRECTORY ${XGBOOST_LIB_DIR})

        # On Windows/MSVC, static libraries don't have a "lib" prefix
        if(MSVC)
            set(XGBOOST_LIB_NAME "xgboost${CMAKE_STATIC_LIBRARY_SUFFIX}")
            set(DMLC_LIB_NAME "dmlc${CMAKE_STATIC_LIBRARY_SUFFIX}")
        else()
            set(XGBOOST_LIB_NAME "libxgboost${CMAKE_STATIC_LIBRARY_SUFFIX}")
            set(DMLC_LIB_NAME "libdmlc${CMAKE_STATIC_LIBRARY_SUFFIX}")
        endif()

        # Enable C++ exceptions for ClangCL.
        if(MSVC)
            set(XGBOOST_CXX_FLAGS
                "-DDMLC_LOG_STACK_TRACE=0 /EHsc")
        else()
            set(XGBOOST_CXX_FLAGS
                "-DDMLC_LOG_STACK_TRACE=0")
        endif()

        # Add XGBoost as external project
        # Disable DMLC stack trace to avoid execinfo.h
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
                -DUSE_CUDA=OFF
                -DUSE_NCCL=OFF
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DBUILD_STATIC_LIB=ON
                ${XGBOOST_EXTRA_CMAKE_ARGS}
            CMAKE_CACHE_ARGS
                "-DCMAKE_C_FLAGS:STRING=-DDMLC_LOG_STACK_TRACE=0"
                "-DCMAKE_CXX_FLAGS:STRING=${XGBOOST_CXX_FLAGS}"
                "-DBUILD_TESTING:BOOL=OFF"
                "-DCMAKE_POLICY_DEFAULT_CMP0091:STRING=NEW"
                "-DCMAKE_MSVC_RUNTIME_LIBRARY:STRING=${CMAKE_MSVC_RUNTIME_LIBRARY}"
            BUILD_BYPRODUCTS
                ${XGBOOST_LIB_DIR}/${XGBOOST_LIB_NAME}
                ${XGBOOST_LIB_DIR}/${DMLC_LIB_NAME}
            LOG_DOWNLOAD ON
            LOG_CONFIGURE ON
            LOG_BUILD ON
            LOG_INSTALL ON
        )

        # Create imported targets
        add_library(xgboost STATIC IMPORTED GLOBAL)
        set_target_properties(xgboost PROPERTIES
            IMPORTED_LOCATION
                ${XGBOOST_LIB_DIR}/${XGBOOST_LIB_NAME}
            INTERFACE_INCLUDE_DIRECTORIES ${XGBOOST_INSTALL_DIR}/include
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ${XGBOOST_INSTALL_DIR}/include
            INTERFACE_COMPILE_DEFINITIONS "DMLC_LOG_STACK_TRACE=0"
        )
        add_dependencies(xgboost xgboost_external)

        if(WIN32 OR MINGW)
            set_target_properties(xgboost PROPERTIES
                INTERFACE_LINK_LIBRARIES "ws2_32")
        endif()

        add_library(dmlc STATIC IMPORTED GLOBAL)
        set_target_properties(dmlc PROPERTIES
            IMPORTED_LOCATION
                ${XGBOOST_LIB_DIR}/${DMLC_LIB_NAME}
            INTERFACE_INCLUDE_DIRECTORIES ${XGBOOST_INSTALL_DIR}/include
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ${XGBOOST_INSTALL_DIR}/include
            INTERFACE_COMPILE_DEFINITIONS "DMLC_LOG_STACK_TRACE=0"
        )
        add_dependencies(dmlc xgboost_external)

        set(OPENZL_XGBOOST_LIBS xgboost dmlc)
    endif()

    add_library(ml_selector
        ml_selector_trainer.cpp
        ml_selector_trainer.h
        ml_features.cpp
        ml_features.h
    )

    target_include_directories(ml_selector
        PUBLIC
            ${PROJECT_SOURCE_DIR}
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
            ${OPENZL_XGBOOST_LIBS}
        )

    if(TARGET xgboost_external)
        add_dependencies(ml_selector xgboost_external)
    endif()

    if (OPENZL_BUILD_TESTS)
        add_executable(test_ml_selector tests/test_mlSelectorGraph.cpp)
        if(TARGET xgboost_external)
            add_dependencies(test_ml_selector xgboost_external)
        endif()

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
            if(TARGET xgboost_external)
                add_dependencies(test_ml_selector_trainer xgboost_external)
            endif()

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
endif()
