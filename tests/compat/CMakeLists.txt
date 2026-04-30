# Copyright (c) Meta Platforms, Inc. and affiliates.

# Compatibility tests for OpenZL public headers under various language modes.
# Each test compiles a trivial program that #includes the umbrella header
# (openzl/openzl.h) and exits. The build is the actual check; the runtime body
# is a no-op.

add_executable(c99_compat c99_compat.c)
target_link_libraries(c99_compat PRIVATE openzl)
set_target_properties(c99_compat PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
if(NOT MSVC)
    target_compile_options(c99_compat PRIVATE -Werror -Wall -Wextra)
endif()
add_test(NAME c99_compat COMMAND c99_compat)
