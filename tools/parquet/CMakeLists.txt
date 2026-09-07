# Copyright (c) Meta Platforms, Inc. and affiliates.

if (OPENZL_BUILD_PARQUET_TOOLS)
  # Prefer a pre-installed Arrow/Parquet (e.g. from conda/pixi) when available,
  # and only fall back to building it from source otherwise. The CONDA_PREFIX
  # hint lets `pixi run` environments be discovered without an extra
  # -DCMAKE_PREFIX_PATH flag.
  find_package(Arrow CONFIG QUIET HINTS "$ENV{CONDA_PREFIX}")
  find_package(Parquet CONFIG QUIET HINTS "$ENV{CONDA_PREFIX}")

  if(Arrow_FOUND AND Parquet_FOUND)
    message(STATUS "Using pre-installed Arrow/Parquet: ${Arrow_DIR}")
    # Parquet::parquet_shared transitively links Arrow::arrow_shared and carries
    # its own include dirs; no ExternalProject or imported targets are required.
    set(OPENZL_PARQUET_LIBS Parquet::parquet_shared Arrow::arrow_shared)
  else()
    message(STATUS
        "Pre-installed Arrow/Parquet not found; building from source via ExternalProject")
    set(ARROW_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/arrow_ep")

    # Point arrow to the zstd build directory
    list(APPEND CMAKE_PREFIX_PATH "${zstd_BINARY_DIR}")

    set(ARROW_CMAKE_ARGS
        -DARROW_PARQUET=ON
        -DARROW_DEPENDENCY_SOURCE=AUTO
        -DARROW_WITH_THRIFT=ON
        -DARROW_WITH_LZ4=OFF # fix
        -DARROW_WITH_SNAPPY=ON
        -DARROW_WITH_ZLIB=ON
        -DARROW_WITH_ZSTD=ON
        -DARROW_MIMALLOC=OFF
        -DCMAKE_INSTALL_PREFIX=${ARROW_PREFIX}/install
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DARROW_CXXFLAGS=-Wno-documentation)
    set(ARROW_LIBDIR ${ARROW_PREFIX}/install/${CMAKE_INSTALL_LIBDIR})

    set(ARROW_DEPS thrift snappy)
    set(thrift_ROOT ${ARROW_PREFIX}/src/arrow_ep-build/thrift_ep-install)
    set(snappy_ROOT ${ARROW_PREFIX}/src/arrow_ep-build/snappy_ep/src/snappy_ep-install)

    foreach (dep ${ARROW_DEPS})
      add_library(${dep} STATIC IMPORTED GLOBAL)
      set(${dep}_LIB ${${dep}_ROOT}/lib/lib${dep}.a)
      list(APPEND ARROW_DEPS_LIBS ${${dep}_LIB})
      file(MAKE_DIRECTORY ${${dep}_ROOT}/include)
      set(${dep}_INCLUDE_DIR ${${dep}_ROOT}/include)
      set_property(TARGET ${dep} PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${${dep}_INCLUDE_DIR})
      set_property(TARGET ${dep} PROPERTY IMPORTED_LOCATION ${${dep}_LIB})
    endforeach()

    set(ZL_ARROW_BUILD_VERSION 20.0.0)
    set(ZL_ARROW_BUILD_SHA256_CHECKSUM 67e31a4f46528634b8c3cbb0dc60ac8f85859d906b400d83d0b6f732b0c5b0e3)
    set(ZL_ARROW_SOURCE_URL
        "https://github.com/apache/arrow/archive/refs/tags/apache-arrow-${ZL_ARROW_BUILD_VERSION}.tar.gz"
    )
    include(ExternalProject)
    ExternalProject_Add(
        arrow_ep
        PREFIX ${ARROW_PREFIX}
        URL ${ZL_ARROW_SOURCE_URL}
        URL_HASH SHA256=${ZL_ARROW_BUILD_SHA256_CHECKSUM}
        SOURCE_SUBDIR cpp
        CMAKE_ARGS ${ARROW_CMAKE_ARGS}
        BUILD_BYPRODUCTS ${ARROW_LIBDIR}/libarrow.a
                         ${ARROW_LIBDIR}/libparquet.a
                         ${ARROW_DEPS_LIBS}
    )

    add_library(arrow STATIC IMPORTED GLOBAL)
    add_library(parquet STATIC IMPORTED GLOBAL)
    add_dependencies(arrow arrow_ep)
    add_dependencies(parquet arrow)

    file(MAKE_DIRECTORY ${ARROW_PREFIX}/install/include)
    set_target_properties(
        arrow parquet PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                                    ${ARROW_PREFIX}/install/include)
    set_target_properties(arrow PROPERTIES IMPORTED_LOCATION
                                            ${ARROW_LIBDIR}/libarrow.a)
    set_target_properties(parquet PROPERTIES IMPORTED_LOCATION
                                            ${ARROW_LIBDIR}/libparquet.a)

      set_property(
        TARGET
        arrow
        PROPERTY
        INTERFACE_LINK_LIBRARIES
          libzstd z ${ARROW_DEPS})
    set_property(TARGET parquet PROPERTY INTERFACE_LINK_LIBRARIES arrow)

    # The parquet target already pulls arrow transitively.
    set(OPENZL_PARQUET_LIBS parquet)
  endif()

  file(
      GLOB_RECURSE make_canonical_parquet_sources
      CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
  add_executable(make_canonical_parquet ${make_canonical_parquet_sources})

  target_link_libraries(
      make_canonical_parquet
      PRIVATE arg openzl ${OPENZL_PARQUET_LIBS} tools_io)
  add_dependencies(make_canonical_parquet arg openzl tools_io)
  if(TARGET arrow_ep)
    add_dependencies(make_canonical_parquet arrow_ep)
  endif()

  apply_openzl_compile_options_to_target(make_canonical_parquet)
endif()
