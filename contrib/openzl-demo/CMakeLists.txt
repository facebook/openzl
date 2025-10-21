# Copyright (c) Meta Platforms, Inc. and affiliates.

find_package(Python 3.8 COMPONENTS Interpreter Development.Module REQUIRED)

# Import nanobind through CMake's find_package mechanism
if (SKBUILD)
  # Must be found via config lookup in scikit-build-core builds driven by pip
  find_package(nanobind CONFIG REQUIRED)
else()
  # Also allow for fetching Nanobind for development.
  # This won't work as expected for real builds of the Python package, but it
  # makes editor integrations work

  # Download the dependencies of Nanobind. The alternative is to download using
  # a git tag, because it is included as a submodule. But that uses ssh to clone,
  # which can require the user to enter their private key password, which is a pain.
  set(TSL_ROBIN_MAP_ENABLE_INSTALL OFF)
  FetchContent_Declare(
      tsl-robin-map # v1.4.0
      URL https://github.com/Tessil/robin-map/archive/refs/tags/v1.4.0.zip
      URL_HASH SHA256=6e0ca21b68f4b029aab12e41428c450a412ab426ed73c59c7ccb360c8392e266
      OVERRIDE_FIND_PACKAGE
  )
  FetchContent_MakeAvailable(tsl-robin-map)

  # Download Nanobind
  set(NB_CREATE_INSTALL_RULES OFF)
  set(NB_USE_SUBMODULE_DEPS OFF)
  FetchContent_Declare(
      nanobind
      URL https://github.com/wjakob/nanobind/archive/refs/tags/v2.7.0.zip
      URL_HASH SHA256=3cadbd08fdf07101d70467adbf9e9a203287519869af6bce1f2ab65df3caafc7
      OVERRIDE_FIND_PACKAGE
  )
  FetchContent_MakeAvailable(nanobind)
endif()

nanobind_add_module(
    _openzl_demo
    ext/openzl_demo.cpp
)
target_link_libraries(
    _openzl_demo
    PRIVATE
    openzl
    openzl_cpp
    tools_training
)

install(
    TARGETS _openzl_demo
    DESTINATION ${Python_SITEARCH}
)
