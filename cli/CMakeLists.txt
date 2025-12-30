# Copyright (c) Meta Platforms, Inc. and affiliates.

file(GLOB cli_root_sources
    CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

file(GLOB cli_root_headers
    CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

# Configure version header for CLI. Accept an explicit CMake cache variable
# `-DVERSION_NUMBER=...` first (CI), then an environment variable
# `VERSION_NUMBER`, then `git describe` as a developer fallback. Finally
# default to "0.0.0-dev".
if(DEFINED VERSION_NUMBER)
    # picked up from -DVERSION_NUMBER=...
    set(VERSION_NUMBER "${VERSION_NUMBER}")
elseif(DEFINED ENV{VERSION_NUMBER})
    set(VERSION_NUMBER $ENV{VERSION_NUMBER})
else()
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_DESCRIBE_OUTPUT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        # Normalize output: remove trailing `-dirty`, strip `-g<sha>`, and
        # remove a leading `v` from tags (so `v1.2.3` -> `1.2.3`).
        string(REGEX REPLACE "-dirty$" "" GIT_DESCRIBE_CLEAN "${GIT_DESCRIBE_OUTPUT}")
        string(REGEX REPLACE "-g[0-9a-fA-F]+$" "" GIT_DESCRIBE_CLEAN "${GIT_DESCRIBE_CLEAN}")
        string(REGEX REPLACE "^v" "" GIT_DESCRIBE_CLEAN "${GIT_DESCRIBE_CLEAN}")
        string(REGEX REPLACE "-[0-9]+$" "" GIT_DESCRIBE_CLEAN "${GIT_DESCRIBE_CLEAN}")
        if(GIT_DESCRIBE_CLEAN)
            set(VERSION_NUMBER ${GIT_DESCRIBE_CLEAN})
        endif()
    endif()

    if(NOT DEFINED VERSION_NUMBER)
        set(VERSION_NUMBER "0.0.0-dev")
    endif()
endif()

# Configure the version header used by the CLI
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
    @ONLY)

add_subdirectory(args)
add_subdirectory(commands)
add_subdirectory(utils)

if (BUILD_TESTS AND BUILD_PYTHON_EXT)
    message(STATUS "adding python tests")
    add_subdirectory(tests)
endif()

add_executable(zli
    ${cli_root_sources}
)
target_link_libraries(zli PRIVATE
    args
    commands
    utils
    logger
)

# there's gotta be a better way than manually specifying openzl/include/
target_include_directories(zli
    PRIVATE ${PROJECT_BINARY_DIR}/include
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR} )

# Include generated headers from this directory (e.g. version.h)
target_include_directories(zli PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
add_dependencies(zli
    args
    commands
    utils
    logger
    xgboost_external
)

apply_openzl_compile_options_to_target(zli)
