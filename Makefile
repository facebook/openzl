# Copyright (c) Meta Platforms, Inc. and affiliates.

# first recipe is default recipe
.PHONY: default
default : zli

# Common repository-wide definitions
include build-scripts/make/zldefs.make

# Provides macros to generate targets
include build-scripts/make/multiconf.make

# =====================================
# ADAPTIVE mode: Per-Target Smart Defaults
# =====================================
# In ADAPTIVE mode, targets add their own appropriate flags
# In coercive modes (OPT, DEV, etc.), these complement are overridden by global settings
ifeq ($(BUILD_TYPE),ADAPTIVE)
    # Production binaries: optimize for performance
    zli: CFLAGS += -g0 -O3
    zli: CXXFLAGS += -g0 -O3
    zli: CPPFLAGS += -DNDEBUG

    libopenzl.a: CFLAGS += -g0 -O3
    libopenzl.so: CFLAGS += -g0 -O3
    libopenzl.a: CPPFLAGS += -DNDEBUG
    libopenzl.so: CPPFLAGS += -DNDEBUG

    # Benchmarks: optimize for representative performance
    unitBench: CFLAGS += -g0 -O3
    unitBench: CPPFLAGS += -DNDEBUG

    # Test programs: enable asserts for correctness checking
    gtests: CFLAGS += -g
    gtests: CXXFLAGS += -g
    gtests: CPPFLAGS += -DZL_ENABLE_ASSERT
endif

# dependencies
ifneq (,$(filter Windows%,$(OS)))
LIBZSTD_SO := deps/zstd/lib/dll/libzstd.dll
LIBLZ4_SO := deps/lz4/lib/liblz4.dll
LIBXGBOOST_SO := deps/xgboost/lib/libxgboost.dll
else ifeq ($(shell uname), Darwin)
LIBZSTD_SO := deps/zstd/lib/libzstd.dylib
LIBLZ4_SO := deps/lz4/lib/liblz4.dylib
LIBXGBOOST_SO := deps/xgboost/lib/libxgboost.dylib
else
LIBZSTD_SO := deps/zstd/lib/libzstd.so
LIBLZ4_SO := deps/lz4/lib/liblz4.so
LIBXGBOOST_SO := deps/xgboost/lib/libxgboost.so
endif

LIBZSTD_A := deps/zstd/lib/libzstd.a
LIBLZ4_A := deps/lz4/lib/liblz4.a
LIBGTEST_A := deps/googletest/lib/libgtest.a
LIBXGBOOST_A := deps/xgboost/lib/libxgboost.a
LIBDMLC_A := deps/xgboost/lib/libdmlc.a

GTEST_HEADERS := deps/googletest/googletest/include/gtest/gtest.h
XGBOOST_HEADER := deps/xgboost/include/xgboost/c_api.h

ifndef SKIP_BUILDDEPS_CHECK
  # Check if any gtest-dependent targets are being built
  GTEST_TARGETS := gtests test all
  BUILDING_GTEST_TARGETS := $(filter $(GTEST_TARGETS),$(MAKECMDGOALS))
  # Only build gtest deps if we're actually building gtest targets
  ifneq ($(BUILDING_GTEST_TARGETS),)
    ifeq ($(wildcard $(GTEST_HEADERS)),)
      $(info Downloading gtest dependency for targets: $(BUILDING_GTEST_TARGETS))
      GTEST_BUILD_RESULT := $(shell $(MAKE) SKIP_BUILDDEPS_CHECK=1 $(GTEST_HEADERS))
      _ := $(shell sync)
      $(if $(shell test -f $(GTEST_HEADERS) && echo EXISTS),,$(error FATAL: $(GTEST_HEADERS) still missing after download attempt))
    endif
  endif

  # Check if xgboost headers are being built
  XGBOOST_TARGETS := zli gtests test all cli_test
  BUILDING_XGBOOST_TARGETS := $(filter $(XGBOOST_TARGETS),$(MAKECMDGOALS))
  ifeq ($(MAKECMDGOALS),)
    # If no targets are specified, assume we're building everything
    BUILDING_XGBOOST_TARGETS := zli
  endif

  ifneq ($(BUILDING_XGBOOST_TARGETS),)
    # Download XGBoost library
    ifeq ($(wildcard $(XGBOOST_HEADER)),)
      $(info Downloading xgboost dependency for targets: $(BUILDING_XGBOOST_TARGETS))
      XGBOOST_BUILD_RESULT := $(shell $(MAKE) SKIP_BUILDDEPS_CHECK=1 $(XGBOOST_HEADER))
      _ := $(shell sync)
      $(if $(shell test -f $(XGBOOST_HEADER) && echo EXISTS),,$(error FATAL: $(XGBOOST_HEADER) still missing after download attempt))
    endif
  endif
endif

# Set EXEC_PREFIX to prefix every build output that is run in tests.
# E.g.qemu
EXEC_PREFIX ?=

# =====================================
# library
# =====================================

LIBCSRCS := $(wildcard $(addsuffix /*.c, $(LIBDIRS)))
LIBASMSRCS := $(wildcard $(addsuffix /*.S, $(LIBDIRS)))
LIBOBJS := $(patsubst %.c,%.o,$(LIBCSRCS)) $(patsubst %.S,%.o,$(LIBASMSRCS))

libopenzl.a:
$(eval $(call static_library,libopenzl.a,$(LIBOBJS),$(LIBZSTD_A) $(LIBLZ4_A)))

libopenzl.so: CFLAGS += -fPIC
$(eval $(call c_dynamic_library,libopenzl.so,$(LIBOBJS),$(LIBZSTD_SO) $(LIBLZ4_SO)))

.PHONY:lib
lib: libopenzl.a libopenzl.so

# =====================================
# Targets
# =====================================

.PHONY: all
all : lib gtests unitBench zli sddl_compiler stream_dump2 examples

# Define a function to generate a list of C++ object files from directory
cxx_objs = $(patsubst %.cpp,%.o,$(wildcard $(addsuffix /*.cpp, $(1))))
c_objs = $(patsubst %.c,%.o,$(wildcard $(addsuffix /*.c, $(1))))

ZLCPP_OBJS := $(call cxx_objs,$(ZLCPP_DIRS))
CLI_CXXOBJS := $(filter-out %zli.o,$(foreach DIR,$(CLIDIRS),$(call cxx_objs,$(DIR))))
ARG_CXXOBJS := $(call cxx_objs,$(ARGDIR))
LOGGER_CXXOBJS := $(call cxx_objs,$(LOGGERDIR))
STREAMDUMP_COBJS := $(call c_objs,$(STREAMDUMPDIR))
CUSTOM_PARSERS_COBJS := $(call c_objs,$(CUSTOMPARSERSDIR))
CUSTOM_PARSERS_CXXOBJS := $(call cxx_objs,$(CUSTOMPARSERSDIR))
# Exclude pytorch_model_compressor.cpp because it depends on Folly.
CUSTOM_PARSERS_CXXOBJS := $(filter-out %/pytorch_model_compressor.o,$(CUSTOM_PARSERS_CXXOBJS))
SHARED_COMPONENTS_CXXOBJS := $(call cxx_objs,$(SHARED_COMPONENTSDIR))
CSV_CXXOBJS := $(call cxx_objs,$(CSVDIR))
CSV_COBJS := $(call c_objs,$(CSVDIR))
PARQUET_CXXOBJS := $(call cxx_objs,$(PARQUETDIR))
PARQUET_COBJS := $(call c_objs,$(PARQUETDIR))
PROFILES_SDDL_COBJS := $(call c_objs,$(PROFILES_SDDL_DIR))
IO_CXXOBJS := $(call cxx_objs,$(IODIR))
VISUALIZER_CXXOBJS := $(call cxx_objs,$(VISUALIZER_CPPDIR))
TRAINING_CXXOBJS := $(call cxx_objs,$(TRAINING_DIRS))
TRAINING_TEST_CXXOBJS := $(call cxx_objs,$(TRAINING_TEST_DIRS))
SDDL_COMPILER_CXXOBJS := $(filter-out %main.o, $(call cxx_objs,$(SDDL_COMPILER_DIR)))
SDDL2_COMPILER_CXXOBJS := $(filter-out %main.o, $(call cxx_objs,$(SDDL2_COMPILER_DIRS)))
SDDL2_ASSEMBLER_CXXOBJS :=  $(filter-out %main.o, $(call cxx_objs,$(SDDL2_ASSEMBLER_DIR)))
ML_SELECTOR_COBJS := $(call c_objs,$(ML_SELECTOR_DIR))
ML_SELECTOR_CXXOBJS := $(call cxx_objs,$(ML_SELECTOR_DIR))

# ML selector files depend on xgboost headers
$(ML_SELECTOR_COBJS) $(ML_SELECTOR_CXXOBJS): | $(XGBOOST_HEADER)

XGBOOST_INCLUDE_PATHS := -Ideps/xgboost/include -Ideps/xgboost/dmlc-core/include # xgboost headers

# Add flags for cross platform compatibility for Windows
zli: LDFLAGS += $(XGBOOST_LDFLAGS)
zli: CPPFLAGS += $(XGBOOST_INCLUDE_PATHS)
zli: LDLIBS += $(XGBOOST_LDLIBS)

gtests: LDFLAGS += $(XGBOOST_LDFLAGS)
gtests: CPPFLAGS += $(XGBOOST_INCLUDE_PATHS)
gtests: LDLIBS += $(XGBOOST_LDLIBS)

$(eval $(call cxx_program,zli, \
	cli/zli.o \
	$(CLI_CXXOBJS) \
	$(ARG_CXXOBJS) \
	$(LOGGER_CXXOBJS) \
	$(CUSTOM_PARSERS_COBJS) \
	$(CUSTOM_PARSERS_CXXOBJS) \
	$(SHARED_COMPONENTS_CXXOBJS) \
	$(CSV_COBJS) \
	$(CSV_CXXOBJS) \
	$(PROFILES_SDDL_COBJS) \
	$(PARQUET_COBJS) \
	$(PARQUET_CXXOBJS) \
	$(VISUALIZER_CXXOBJS) \
	$(IO_CXXOBJS) \
	$(TRAINING_CXXOBJS) \
	$(SDDL_COMPILER_CXXOBJS) \
	$(SDDL2_COMPILER_CXXOBJS) \
 	$(SDDL2_ASSEMBLER_CXXOBJS) \
	$(ML_SELECTOR_COBJS) \
	$(ML_SELECTOR_CXXOBJS) \
	$(ZLCPP_OBJS) \
	$(LIBOBJS), \
	$(LIBZSTD_A) $(LIBLZ4_A) $(LIBXGBOOST_A) $(LIBDMLC_A)))

.PHONY: examples
examples: zs2_pipeline zs2_trygraph zs2_selector zs2_struct zs2_round_trip

.PHONY: test
test : gtests zs2_test
	$(EXEC_PREFIX) ./gtests

# Python bindings for openzl.ext module (required for ML tests)
.PHONY: python-bindings
python-bindings:
	@echo "Building and installing openzl Python bindings..."
	pip install --quiet py/

.PHONY: cli_test
cli_test: zli python-bindings
	cd cli/tests && python3 cli_integration_tests.py ../../zli

.PHONY: check-python-format
check-python-format:
	@./scripts/check_python_format.sh

.PHONY: fix-python-format
fix-python-format:
	@./scripts/check_python_format.sh --fix

.PHONY: zs2_test
zs2_test : examples
	$(EXEC_PREFIX) ./zs2_pipeline
	$(EXEC_PREFIX) ./zs2_trygraph

# ********     Tools     ********

UNITBENCH_COBJS := $(foreach DIR,$(UNITBENCH_DIRS),$(call c_objs,$(DIR)))
$(eval $(call c_program_shared_o,unitBench,tools/time/timefn.o tools/fileio/fileio.o $(UNITBENCH_COBJS) $(LIBOBJS),$(LIBZSTD_A) $(LIBLZ4_A)))

stream_dump2:
$(eval $(call c_program_shared_o,stream_dump2, \
    $(STREAMDUMP_COBJS) tools/fileio/fileio.o $(LIBOBJS),$(LIBZSTD_A) $(LIBLZ4_A)))

$(eval $(call cxx_program,sddl_compiler, \
	$(SDDL_COMPILER_DIR)/main.o \
	$(SDDL_COMPILER_CXXOBJS) \
	$(ZLCPP_OBJS), \
	libopenzl.a \
	$(LIBZSTD_A) $(LIBLZ4_A)))

# Selection of gtest units (by file name convention)
CXX_FILE_OBJS := $(notdir $(CXX_OBJS))
GTEST_FILEO := $(filter test_%.o,$(CXX_FILE_OBJS))
GTEST_FILEO += $(filter %Test.o,$(CXX_FILE_OBJS))
GTEST_FILTER_LIST := VersionTest.o NoIntrospectionTest.o
GTEST_FILEO := $(filter-out $(GTEST_FILTER_LIST),$(GTEST_FILEO))

ALL_TEST_OBJS := $(patsubst %.cpp,%.o,$(foreach dir,$(TESTSDIRS) $(ML_SELECTOR_TESTS_DIR),$(wildcard $(dir)/*.cpp)))
GTEST_OBJS := $(foreach name,$(GTEST_FILEO),$(filter %/$(name),$(ALL_TEST_OBJS)))

# Other module objects used in gtests
DATAGEN_OBJS := \
	tests/datagen/structures/CompressorProducer.o \
	tests/datagen/structures/LocalParamsProducer.o \
	tests/datagen/structures/openzl/StringInputProducer.o \
	tests/datagen/InputExpander.o
SERIALIZATION_TEST_OBJS := \
	tests/serialization/GraphBuilder.o \
	tests/serialization/GraphBuilderUtils.o
TEST_REGISTRY_SRCS = $(wildcard $(addsuffix /*.cpp, $(TEST_REGISTRY_DIRS)))
TEST_REGISTRY_OBJS = $(patsubst %.cpp,%.o,$(TEST_REGISTRY_SRCS))
CLI_TEST_OBJS := $(filter-out %/test_%.o test_%.o,$(foreach DIR,$(CLI_TEST_DIRS),$(call cxx_objs,$(DIR))))
ZLCPP_TEST_OBJS := $(call cxx_objs,$(ZLCPP_TEST_DIR))

ALL_GTESTS_OBJS := \
	tests/gtest_main.o \
	tools/time/timefn.o \
	tests/utils.o \
	tests/local_params_utils.o \
	tests/ml_selector_utils.o \
	tests/unittest/common/test_errors_in_c.o \
	tests/unittest/common/sha_vec.o \
	tests/compress/ml_selectors/test_zstrong_ml_core_models.o \
	$(GTEST_OBJS) \
	$(ZLCPP_TEST_OBJS) \
	$(CLI_CXXOBJS) \
	$(CLI_TEST_OBJS) \
	$(ARG_CXXOBJS) \
	$(LOGGER_CXXOBJS) \
	$(CUSTOM_PARSERS_COBJS) \
	$(CUSTOM_PARSERS_CXXOBJS) \
	$(SHARED_COMPONENTS_CXXOBJS) \
	$(CSV_CXXOBJS) \
	$(CSV_COBJS) \
	$(PROFILES_SDDL_COBJS) \
	$(PARQUET_CXXOBJS) \
	$(PARQUET_COBJS) \
	$(VISUALIZER_CXXOBJS) \
	$(IO_CXXOBJS) \
	$(TRAINING_CXXOBJS) \
	$(SDDL_COMPILER_CXXOBJS) \
	$(SDDL2_COMPILER_CXXOBJS) \
	$(SDDL2_ASSEMBLER_CXXOBJS) \
	$(ML_SELECTOR_COBJS) \
	$(ML_SELECTOR_CXXOBJS) \
	$(DATAGEN_OBJS) \
	$(SERIALIZATION_TEST_OBJS) \
	$(TEST_REGISTRY_OBJS) \
	$(ZLCPP_OBJS) \
	$(LIBOBJS)

gtests: $(LIBGTEST_A) $(LIBZSTD_A) $(LIBLZ4_A) $(LIBXGBOOST_A)
gtests: CPPFLAGS += -Ideps/googletest/googletest/include
gtests: CXXFLAGS += -Wno-undef -Wno-sign-compare
gtests: LDLIBS   += -lpthread
$(eval $(call cxx_program,gtests, \
	$(ALL_GTESTS_OBJS), \
	$(LIBGTEST_A) $(LIBZSTD_A) $(LIBLZ4_A) $(LIBXGBOOST_A) $(LIBDMLC_A)))

# ********     Examples     ********

zs2_pipeline:
$(eval $(call c_program_shared_o,zs2_pipeline,examples/zs2_pipeline.o tools/fileio/fileio.o $(LIBOBJS),$(LIBZSTD_A) $(LIBLZ4_A)))

zs2_struct:
$(eval $(call c_program_shared_o,zs2_struct,examples/zs2_struct.o tools/fileio/fileio.o $(LIBOBJS),$(LIBZSTD_A) $(LIBLZ4_A)))

zs2_trygraph:
$(eval $(call c_program_shared_o,zs2_trygraph,examples/zs2_trygraph.o $(LIBOBJS),$(LIBZSTD_A) $(LIBLZ4_A)))

zs2_selector:
$(eval $(call c_program_shared_o,zs2_selector,examples/zs2_selector.o tools/fileio/fileio.o $(LIBOBJS),$(LIBZSTD_A) $(LIBLZ4_A)))

zs2_round_trip:
$(eval $(call cxx_program_shared_o,zs2_round_trip,tests/round_trip.o tools/fileio/fileio.o $(SHARED_COMPONENTS_CXXOBJS) $(LIBOBJS),$(LIBZSTD_A) $(LIBLZ4_A)))

# ********     Compatibility tests     ********

# Compile-only check that public headers remain usable from a strict C99 TU.
.PHONY: c99_compat
c99_compat:
	$(CC) -std=c99 -Werror -Wall -Wextra -Iinclude \
	    -c tests/compat/c99_compat.c -o /dev/null

# ********     Cleaning     ********

.PHONY: clean
clean:
	# note: a lot is done within multiconf.make
	@echo Cleaning completed

#special cases : these targets require additional flags to compile without warnings
$(CACHE_ROOT)/%/src/openzl/common/errors.o : CFLAGS += -Wno-format-nonliteral
$(CACHE_ROOT)/%/src/openzl/common/logging.o : CFLAGS += -Wno-format-nonliteral
$(CACHE_ROOT)/%/src/openzl/codecs/rolz/encode_experimental_enc.o : CFLAGS += -Wno-uninitialized
$(CACHE_ROOT)/%/tests/unittest/common/test_debug.o: CXXFLAGS += -Wno-ignored-attributes

# ********     Dependencies     ********

CURL ?= curl
GIT ?= git
TAR ?= tar

# Use this target as a work-around if dependencies are not correctly built
# automatically.
.PHONY : builddeps
builddeps : $(LIBGTEST_A) $(LIBZSTD_A) $(LIBZSTD_SO) $(LIBLZ4_A) $(LIBLZ4_SO)

.PHONY: cleandeps
cleandeps:
	$(RM) -r deps/googletest deps/googletest.tar.gz
	-$(GIT) submodule deinit -f deps/zstd 2> /dev/null
	$(RM) -r deps/zstd deps/$(ZSTD_DIRNAME) $(ZSTD_TARBALL)
	-$(GIT) submodule deinit -f deps/lz4 2> /dev/null
	$(RM) -r deps/lz4 deps/$(LZ4_DIRNAME) $(LZ4_TARBALL)
	$(RM) -r deps/xgboost deps/xgboost.tar.gz

# Variables are by default not exported, but when they're passed on the CLI,
# they are exported. We do not want to pass super restrictive flags to our
# dependencies, like zstd, which will fail to build with them.
unexport CFLAGS
unexport CXXFLAGS

# Zstandard

ZSTD_LIBDIR := deps/zstd/lib
ZSTD_HEADER := $(ZSTD_LIBDIR)/zstd.h
ZSTD_MAKEFILE := $(ZSTD_LIBDIR)/Makefile

ZSTD_VERSION ?= 1.5.7
ZSTD_DIRNAME := zstd-$(ZSTD_VERSION)
ZSTD_TARBALL := deps/$(ZSTD_DIRNAME).tar.gz

$(ZSTD_TARBALL):
	$(MKDIR) -p deps
	$(CURL) -L https://github.com/facebook/zstd/releases/download/v$(ZSTD_VERSION)/$(ZSTD_DIRNAME).tar.gz -o $@

.PHONY: zstd-fallback
zstd-fallback: $(ZSTD_TARBALL)
	$(RM) -r deps/$(ZSTD_DIRNAME)
	$(TAR) -xzf $(ZSTD_TARBALL) -C deps
	$(RM) -r deps/zstd
	mv deps/$(ZSTD_DIRNAME) deps/zstd

$(ZSTD_HEADER):
	-$(GIT) submodule update --init --single-branch --depth 1 deps/zstd
	if [ ! -f $@ ]; then \
		echo "Falling back to tarball download and extraction for zstd"; \
		$(MAKE) zstd-fallback; \
	fi

$(ZSTD_MAKEFILE): $(ZSTD_HEADER)
	touch $@

$(LIBZSTD_SO) : MAKEOVERRIDES=
$(LIBZSTD_SO) : $(ZSTD_MAKEFILE)
	$(MAKE) -C $(ZSTD_LIBDIR) libzstd

$(LIBZSTD_A) : MAKEOVERRIDES=
$(LIBZSTD_A) : $(ZSTD_MAKEFILE)
	$(MAKE) -C $(ZSTD_LIBDIR) libzstd.a

# LZ4
LZ4_LIBDIR := deps/lz4/lib
LZ4_HEADER := $(LZ4_LIBDIR)/lz4.h
LZ4_MAKEFILE := $(LZ4_LIBDIR)/Makefile

LZ4_VERSION ?= 1.10.0
LZ4_DIRNAME := lz4-$(LZ4_VERSION)
LZ4_TARBALL := deps/$(LZ4_DIRNAME).tar.gz

$(LZ4_TARBALL):
	$(MKDIR) -p deps
	$(CURL) -L https://github.com/lz4/lz4/releases/download/v$(LZ4_VERSION)/$(LZ4_DIRNAME).tar.gz -o $@

.PHONY: lz4-fallback
lz4-fallback: $(LZ4_TARBALL)
	$(RM) -r deps/$(LZ4_DIRNAME)
	$(TAR) -xzf $(LZ4_TARBALL) -C deps
	$(RM) -r deps/lz4
	mv deps/$(LZ4_DIRNAME) deps/lz4

$(LZ4_HEADER):
	-$(GIT) submodule update --init --single-branch --depth 1 deps/lz4
	if [ ! -f $@ ]; then \
		echo "Falling back to tarball download and extraction for lz4"; \
		$(MAKE) lz4-fallback; \
	fi

$(LZ4_MAKEFILE): $(LZ4_HEADER)
	touch $@

$(LIBLZ4_SO) : MAKEOVERRIDES=
$(LIBLZ4_SO) : $(LZ4_MAKEFILE)
	$(MAKE) -C $(LZ4_LIBDIR) liblz4

$(LIBLZ4_A) : MAKEOVERRIDES=
$(LIBLZ4_A) : $(LZ4_MAKEFILE)
	$(MAKE) -C $(LZ4_LIBDIR) liblz4.a

# Google Test

GTEST_VERSION ?= 1.17.0
GTEST_DIRNAME := googletest-$(GTEST_VERSION)
GTEST_TARBALL := deps/$(GTEST_DIRNAME).tar.gz

$(GTEST_TARBALL):
	$(MKDIR) -p deps
	$(CURL) -L https://github.com/google/googletest/releases/download/v$(GTEST_VERSION)/$(GTEST_DIRNAME).tar.gz -o $@

.PHONY: gtest-fallback
gtest-fallback: $(GTEST_TARBALL)
	$(RM) -r deps/$(GTEST_DIRNAME)
	$(TAR) -xzf $(GTEST_TARBALL) -C deps
	$(RM) -r deps/googletest
	mv deps/$(GTEST_DIRNAME) deps/googletest

# Ensure headers are available - prefer submodule, fallback to tarball
$(GTEST_HEADERS):
	-$(GIT) submodule update --init --single-branch --depth 1 deps/googletest
	if [ ! -f $@ ]; then \
		echo "Falling back to tarball download and extraction for googletest"; \
		$(MAKE) gtest-fallback; \
	fi

$(LIBGTEST_A) : MAKEOVERRIDES=
$(LIBGTEST_A) : $(GTEST_HEADERS)
	cd deps/googletest && cmake .
	$(MAKE) -C deps/googletest

# XGBoost
XGBOOST_LIBDIR := deps/xgboost/lib

XGBOOST_VERSION ?= 3.1.0
XGBOOST_DIRNAME := xgboost-src-$(XGBOOST_VERSION)
XGBOOST_TARBALL := deps/$(XGBOOST_DIRNAME).tar.gz

# Common CMake flags for xgboost shared library build
XGBOOST_CMAKE_COMMON := -DBUILD_STATIC_LIB=OFF -DUSE_OPENMP=OFF \
	-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$(abspath $(XGBOOST_LIBDIR))

# Platform-specific CMake flags for xgboost
XGBOOST_CMAKE_PLATFORM :=
XGBOOST_LDFLAGS :=
XGBOOST_LDLIBS :=
ifneq (,$(filter $(SMALL_CMD_LINE),$(UNAME)))
    # MinGW/MSYS: Link ws2_32 for Windows socket functions
    XGBOOST_CMAKE_PLATFORM := -DCMAKE_CXX_STANDARD_LIBRARIES="-lws2_32" \
        -DCMAKE_SHARED_LINKER_FLAGS="-lws2_32"
    XGBOOST_LDFLAGS += -L$(abspath $(XGBOOST_LIBDIR))
    XGBOOST_LDLIBS += -lws2_32
else ifeq ($(shell uname),Darwin)
    # macOS: Set install_name to absolute path so dyld can find the library
    XGBOOST_CMAKE_PLATFORM := -DCMAKE_INSTALL_NAME_DIR=$(abspath $(XGBOOST_LIBDIR)) \
        -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON -DCMAKE_MACOSX_RPATH=ON
endif

$(XGBOOST_TARBALL):
	$(MKDIR) -p deps
	$(CURL) -L https://github.com/dmlc/xgboost/releases/download/v$(XGBOOST_VERSION)/$(XGBOOST_DIRNAME).tar.gz -o $@

.PHONY: xgboost-fallback
xgboost-fallback: $(XGBOOST_TARBALL)
	$(RM) -r deps/xgboost
	$(TAR) -xzf $(XGBOOST_TARBALL) -C deps
	@if [ -d deps/$(XGBOOST_DIRNAME) ] && [ ! -d deps/xgboost ]; then \
		mv deps/$(XGBOOST_DIRNAME) deps/xgboost; \
	fi

$(XGBOOST_HEADER):
	-$(GIT) submodule update --init --recursive --single-branch --depth 1 deps/xgboost
	if [ ! -f $@ ]; then \
		echo "Falling back to tarball download and extraction for xgboost"; \
		$(MAKE) xgboost-fallback; \
	fi

# Build shared library only after static library is done (to avoid parallel cmake conflicts)
$(LIBXGBOOST_SO) : MAKEOVERRIDES=
$(LIBXGBOOST_SO) : $(LIBXGBOOST_A)
	$(MKDIR) -p $(XGBOOST_LIBDIR)
	cd deps/xgboost && mkdir -p build-shared && cd build-shared && \
		cmake .. $(XGBOOST_CMAKE_COMMON) $(XGBOOST_CMAKE_PLATFORM) && $(MAKE)
ifeq ($(shell uname),Darwin)
	install_name_tool -id "$(abspath $(XGBOOST_LIBDIR))/libxgboost.dylib" \
		"$(abspath $(XGBOOST_LIBDIR))/libxgboost.dylib" || true
endif

$(LIBXGBOOST_A) : MAKEOVERRIDES=
$(LIBXGBOOST_A) : $(XGBOOST_HEADER)
	$(MKDIR) -p $(XGBOOST_LIBDIR)
	cd deps/xgboost && mkdir -p build && cd build && cmake .. -DBUILD_STATIC_LIB=ON -DUSE_OPENMP=OFF -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(abspath $(XGBOOST_LIBDIR)) && $(MAKE)

# libdmlc.a is built as part of xgboost static build
$(LIBDMLC_A): $(LIBXGBOOST_A)
