# Copyright (c) Meta Platforms, Inc. and affiliates.
# WASM binding layer - C++ with a C ABI, JS glue in js/.
# Native builds compile the binding as a library for CTest. Emscripten builds
# compile the same source directly into the WASM executable below.
set(OPENZL_WASM_SOURCE src/openzl_wasm.cpp)

if(NOT EMSCRIPTEN)
  # openzl_wasm_lib is used only for native testing in non-Emscripten builds
  add_library(openzl_wasm_lib STATIC ${OPENZL_WASM_SOURCE})

  target_include_directories(openzl_wasm_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
  )

  target_link_libraries(openzl_wasm_lib PRIVATE openzl openzl_profile_graphs)
  apply_openzl_compile_options_to_target(openzl_wasm_lib)

  # Native test - runs under both `buck test` and, with
  # -DOPENZL_BUILD_TESTS=ON, `ctest -R WasmBinding`. gtest_discover_tests
  # registers the gtest names, not the executable name, so the ctest filter
  # matches the suites.
  if(OPENZL_BUILD_TESTS)
    add_executable(test_wasm_binding tests/test_wasm_binding.cpp)
    target_include_directories(test_wasm_binding PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../..
      ${CMAKE_CURRENT_SOURCE_DIR}/../../include
      ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    )
    target_link_libraries(test_wasm_binding
      PRIVATE
        openzl_wasm_lib
        openzl
        GTest::gtest_main
    )
    apply_openzl_compile_options_to_target(test_wasm_binding)
    gtest_discover_tests(test_wasm_binding)
  endif()
endif()

if(NOT DEFINED OPENZL_BUILD_WASM OR NOT OPENZL_BUILD_WASM)
  return()
endif()

if(NOT EMSCRIPTEN)
  message(WARNING
    "OPENZL_BUILD_WASM=ON but Emscripten not detected, skipping WASM artifact")
  return()
endif()

add_executable(openzl_wasm ${OPENZL_WASM_SOURCE})

target_include_directories(openzl_wasm PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
  ${CMAKE_CURRENT_SOURCE_DIR}/../../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(openzl_wasm PRIVATE openzl openzl_profile_graphs)

# -sMEMORY64=1 / -sWASM_BIGINT=1 are set globally in the top-level file so
# every object, including the core library, uses the wasm64 ABI.
target_compile_options(openzl_wasm PRIVATE -O3)
target_link_options(openzl_wasm PRIVATE
  -O3                        # optimise the linked wasm
  -sWASM=1                   # emit wasm, not asm.js
  -sALLOW_MEMORY_GROWTH=1    # heap can grow; JS must re-read mod.HEAPU8
  -sINITIAL_MEMORY=32MB      # starting heap size
  -sMODULARIZE=1             # export a factory function, not globals
  -sEXPORT_ES6=1             # emit an ES module
  -sEXPORT_NAME=openzlModule # name of the factory; js/wasm_api.js imports this
  # Nothing to list: every entry point, the allocator included, exports itself
  # via EMSCRIPTEN_KEEPALIVE. The empty value overrides the _main default.
  -sEXPORTED_FUNCTIONS=
  # JS helpers to keep. HEAPU64 reads the 8-byte out-params of a wasm64 build,
  # HEAPF64 the benchmark timings; UTF8ToString reads the error message.
  -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU64,HEAPF64,UTF8ToString
  -sENVIRONMENT=web,worker,node
)

set_target_properties(openzl_wasm PROPERTIES OUTPUT_NAME "openzl" SUFFIX ".js")

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/openzl.js
  ${CMAKE_CURRENT_BINARY_DIR}/openzl.wasm
  DESTINATION wasm
  OPTIONAL
)
