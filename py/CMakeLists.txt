# Copyright (c) Meta Platforms, Inc. and affiliates.

if (NOT SKBUILD)
  message(WARNING "\
  This CMake file is meant to be executed using 'scikit-build-core'.
  Running it directly will almost certainly not produce the desired
  result. If you are a user trying to install this package, use the
  command below, which will install all necessary build dependencies,
  compile the package in an isolated environment, and then install it.
  =====================================================================
   $ pip install .
  =====================================================================
  If you are a software developer, and this is your own package, then
  it is usually much more efficient to install the build dependencies
  in your environment once and use the following command that avoids
  a costly creation of a new virtual environment at every compilation:
  =====================================================================
   $ pip install nanobind scikit-build-core[pyproject]
   $ pip install --no-build-isolation -ve .
  =====================================================================
  You may optionally add -Ceditable.rebuild=true to auto-rebuild when
  the package is imported. Otherwise, you need to rerun the above
  after editing C++ files.")
endif()

if (OPENZL_BUILD_SHARED_LIBS)
  message(FATAL_ERROR "Python extension is not compatible with shared libraries")
endif()

# Find Python
if (CMAKE_VERSION VERSION_LESS 3.18)
  set(DEV_MODULE Development)
else()
  set(DEV_MODULE Development.Module)
endif()
find_package(Python 3.8
    REQUIRED
    COMPONENTS Interpreter ${DEV_MODULE}
    OPTIONAL_COMPONENTS Development.SABIModule)

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

# Define the Python extension
nanobind_add_module(
  ext

  # Target the stable ABI for Python 3.12+, which reduces
  # the number of binary wheels that must be built. This
  # does nothing on older Python versions
  STABLE_ABI

  # Sources
  ext/openzl/ext.cpp
  ext/openzl/ext/graphs.cpp
  ext/openzl/ext/nodes.cpp)

# Add include directories
target_include_directories(
    ext
    BEFORE
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/py/ext>)

# Add dependencies
target_link_libraries(ext PRIVATE openzl_cpp openzl sddl_compiler_lib sddl2_compiler_lib)
apply_openzl_compile_options_to_target(ext NO_WARNINGS)

install(TARGETS ext LIBRARY DESTINATION openzl)

if (SKBUILD AND NOT (Python_VERSION_MAJOR EQUAL 3 AND Python_VERSION_MINOR LESS 9))
  # Only build stubs for scikit-build-core builds, as otherwise the
  # dependencies might not be installed.
  # CI also can't build stubs on MacOS for Python3.8 because it cannot run
  # arm64 builds, since the Python3.8 build is x86_64.
  # So just skip stub generation for Python3.8 and older.
  # See https://github.com/pypa/cibuildwheel/pull/1169
  nanobind_add_stub(
    openzl_ext_stub

    MODULE ext
    PYTHON_PATH $<TARGET_FILE_DIR:ext>
    OUTPUT stubs/openzl/ext.pyi
    DEPENDS ext
  )
  nanobind_add_stub(
    openzl_ext_graphs_stub

    MODULE ext.graphs
    PYTHON_PATH $<TARGET_FILE_DIR:ext>
    OUTPUT stubs/openzl/ext/graphs.pyi
    DEPENDS ext
  )
  nanobind_add_stub(
    openzl_ext_nodes_stub

    MODULE ext.nodes
    PYTHON_PATH $<TARGET_FILE_DIR:ext>
    OUTPUT stubs/openzl/ext/nodes.pyi
    DEPENDS ext
  )
  install(DIRECTORY ${PROJECT_BINARY_DIR}/py/stubs/openzl/ DESTINATION openzl/)
endif()
