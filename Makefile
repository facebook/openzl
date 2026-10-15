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
else ifeq ($(shell uname), Darwin)
LIBZSTD_SO := deps/zstd/lib/libzstd.dylib
LIBLZ4_SO := deps/lz4/lib/liblz4.dylib
else
LIBZSTD_SO := deps/zstd/lib/libzstd.so
LIBLZ4_SO := deps/lz4/lib/liblz4.so
endif

LIBZSTD_A := deps/zstd/lib/libzstd.a
LIBLZ4_A := deps/lz4/lib/liblz4.a
LIBGTEST_A := deps/googletest/lib/libgtest.a
LIBXGBOOST_A := deps/xgboost/lib/libxgboost.a
LIBDMLC_A := deps/xgboost/lib/libdmlc.a

ZSTD_HEADER := deps/zstd/lib/zstd.h
LZ4_HEADER := deps/lz4/lib/lz4.h
GTEST_HEADERS := deps/googletest/googletest/include/gtest/gtest.h
XGBOOST_HEADER := deps/xgboost/include/xgboost/c_api.h

# Set EXEC_PREFIX to prefix every build output that is run in tests.
# E.g.qemu
EXEC_PREFIX ?=

# Discovered objects below directories $(1), except those of tests/ subdirectories
objs_below = $(foreach o,$(filter $(addsuffix /%,$(1)),$(C_OBJS) $(CXX_OBJS) $(ASM_OBJS)),$(if $(findstring /tests/,$(o)),,$(o)))


# =====================================
# library
# =====================================

LIBOBJS := $(call objs_below,src)

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

# Source directories of zli, besides the library
ZLI_DIRS := cli custom_parsers cpp/src tools/arg tools/io tools/logger tools/ml_selector \
	tools/sddl/compiler tools/sddl2/assembler tools/sddl2/compiler tools/training \
	tools/zl_visualizer/compression_introspection
# Objects of zli except its main, also linked into gtests.
# Exclude pytorch_model_compressor.cpp because it depends on Folly.
ZLI_COMMON_OBJS := $(filter-out cli/zli.o %/main.o custom_parsers/pytorch_model_compressor.o,$(call objs_below,$(ZLI_DIRS))) $(LIBOBJS)

ML_SELECTOR_CPPFLAGS := -Ideps/xgboost/include -Ideps/xgboost/dmlc-core/include -DDMLC_LOG_STACK_TRACE=0 -DOPENZL_HAS_ML_SELECTOR_TRAINER=1

# Add flags for cross platform compatibility for Windows
zli: LDFLAGS += $(XGBOOST_LDFLAGS)
zli: CPPFLAGS += $(ML_SELECTOR_CPPFLAGS) -DZDICT_STATIC_LINKING_ONLY
zli: LDLIBS += $(XGBOOST_LDLIBS)

gtests: LDFLAGS += $(XGBOOST_LDFLAGS)
gtests: CPPFLAGS += $(ML_SELECTOR_CPPFLAGS) -DZDICT_STATIC_LINKING_ONLY
gtests: LDLIBS += $(XGBOOST_LDLIBS)

$(eval $(call cxx_program,zli, \
	cli/zli.o $(ZLI_COMMON_OBJS), \
	$(LIBZSTD_A) $(LIBLZ4_A) $(LIBXGBOOST_A) $(LIBDMLC_A)))

.PHONY: examples
examples: zs2_pipeline zs2_trygraph zs2_selector zs2_struct zs2_round_trip itch

.PHONY: test
test : gtests test-zs2 test-cli
	$(EXEC_PREFIX) ./gtests

# Python bindings for openzl.ext module (required for ML tests)
.PHONY: python-bindings
python-bindings:
	@echo "Building and installing openzl Python bindings..."
	pip install --quiet py/

.PHONY: test-cli
test-cli: zli
	cd cli/tests && python3 cli_integration_tests.py ../../zli

.PHONY: test-train
test-train: zli
	cd cli/tests && python3 cli_train_tests.py ../../zli

.PHONY: test-formats
test-formats: zli
	cd cli/tests && python3 cli_formats_tests.py ../../zli

.PHONY: check-python-format
check-python-format:
	@./scripts/check_python_format.sh

.PHONY: fix-python-format
fix-python-format:
	@./scripts/check_python_format.sh --fix

.PHONY: test-zs2
test-zs2 : examples
	$(EXEC_PREFIX) ./zs2_pipeline
	$(EXEC_PREFIX) ./zs2_trygraph

# ********     Tools     ********

$(eval $(call c_program_shared_o,unitBench,tools/time/timefn.o tools/fileio/fileio.o $(call objs_below,benchmark/unitBench) $(LIBOBJS),$(LIBZSTD_A) $(LIBLZ4_A)))

stream_dump2:
$(eval $(call c_program_shared_o,stream_dump2, \
    $(call objs_below,tools/streamdump) tools/fileio/fileio.o $(LIBOBJS),$(LIBZSTD_A) $(LIBLZ4_A)))

$(eval $(call cxx_program,sddl_compiler, \
	$(call objs_below,tools/sddl/compiler cpp/src) \
	$(LIBOBJS), \
	$(LIBZSTD_A) $(LIBLZ4_A)))

# Selection of gtest units (by file name convention)
CXX_FILE_OBJS := $(notdir $(CXX_OBJS))
GTEST_FILEO := $(filter test_%.o,$(CXX_FILE_OBJS))
GTEST_FILEO += $(filter %Test.o,$(CXX_FILE_OBJS))
GTEST_FILTER_LIST := VersionTest.o NoIntrospectionTest.o
GTEST_FILEO := $(filter-out $(GTEST_FILTER_LIST),$(GTEST_FILEO))

ALL_TEST_OBJS := $(filter tests/% cli/tests/% tools/ml_selector/tests/%,$(CXX_OBJS))
GTEST_OBJS := $(filter $(addprefix %/,$(GTEST_FILEO)),$(ALL_TEST_OBJS))

# Other module objects used in gtests
DATAGEN_OBJS := \
	tests/datagen/structures/CompressorProducer.o \
	tests/datagen/structures/LocalParamsProducer.o \
	tests/datagen/structures/openzl/StringInputProducer.o \
	tests/datagen/InputExpander.o
SERIALIZATION_TEST_OBJS := \
	tests/serialization/GraphBuilder.o \
	tests/serialization/GraphBuilderUtils.o
TEST_REGISTRY_OBJS := $(filter tests/registry/%,$(CXX_OBJS))
ZLCPP_TEST_OBJS := $(filter-out cpp/tests/experimental/%,$(filter cpp/tests/%,$(CXX_OBJS)))

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
	$(DATAGEN_OBJS) \
	$(SERIALIZATION_TEST_OBJS) \
	$(TEST_REGISTRY_OBJS) \
	$(ZLI_COMMON_OBJS)

# Objects wait for the headers of the dependencies they include, which are fetched on demand
$(call mcm_order_deps,$(C_OBJS) $(CPP_OBJS),$(ZSTD_HEADER) $(LZ4_HEADER))
$(call mcm_order_deps,$(call objs_below,tools/ml_selector),$(XGBOOST_HEADER))
$(call mcm_order_deps,$(filter tests/% %/tests/%,$(ALL_GTESTS_OBJS)),$(GTEST_HEADERS))

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
$(eval $(call cxx_program_shared_o,zs2_round_trip,tests/round_trip.o tools/fileio/fileio.o $(call objs_below,custom_parsers/shared_components) $(LIBOBJS),$(LIBZSTD_A) $(LIBLZ4_A)))

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
$(MCM_CACHE_ROOT)/%/src/openzl/common/errors.o : CFLAGS += -Wno-format-nonliteral
$(MCM_CACHE_ROOT)/%/src/openzl/common/logging.o : CFLAGS += -Wno-format-nonliteral
$(MCM_CACHE_ROOT)/%/src/openzl/codecs/rolz/encode_experimental_enc.o : CFLAGS += -Wno-uninitialized
$(MCM_CACHE_ROOT)/%/tests/unittest/common/test_debug.o: CXXFLAGS += -Wno-ignored-attributes

# ********     Dependencies     ********

# Provides fetch_dependency, check-dependency-pins and cleandeps
include build-scripts/make/deps.make

# Source files of a dependency, in directories $(1) with extensions $(2).
# Its library is rebuilt when any of them changes; since the sub-build may then
# find nothing to do, the library is touched afterwards.
dep_srcs = $(wildcard $(foreach d,$(1),$(addprefix $(d)/*.,$(2))))

# Builds all dependencies ahead of time
.PHONY : builddeps
builddeps : $(LIBGTEST_A) $(LIBZSTD_A) $(LIBZSTD_SO) $(LIBLZ4_A) $(LIBLZ4_SO)

# Variables are by default not exported, but when they're passed on the CLI,
# they are exported. We do not want to pass super restrictive flags to our
# dependencies, like zstd, which will fail to build with them.
unexport CFLAGS
unexport CXXFLAGS

# Zstandard
ZSTD_VERSION ?= 1.5.7
ZSTD_SHA256 ?= eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
$(eval $(call fetch_dependency,zstd,lib/zstd.h,v$(ZSTD_VERSION),https://github.com/facebook/zstd/releases/download/v$(ZSTD_VERSION)/zstd-$(ZSTD_VERSION).tar.gz,$(ZSTD_SHA256)))

ZSTD_LIBDIR := deps/zstd/lib
ZSTD_SRCS := $(call dep_srcs,$(ZSTD_LIBDIR) $(ZSTD_LIBDIR)/*,c h S)

$(LIBZSTD_SO) : MAKEOVERRIDES=
$(LIBZSTD_SO) : $(ZSTD_HEADER) $(ZSTD_SRCS)
	$(MAKE) -C $(ZSTD_LIBDIR) libzstd
	touch $@

$(LIBZSTD_A) : MAKEOVERRIDES=
$(LIBZSTD_A) : $(ZSTD_HEADER) $(ZSTD_SRCS)
	$(MAKE) -C $(ZSTD_LIBDIR) libzstd.a
	touch $@

# LZ4
LZ4_VERSION ?= 1.10.0
LZ4_SHA256 ?= 537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b
$(eval $(call fetch_dependency,lz4,lib/lz4.h,v$(LZ4_VERSION),https://github.com/lz4/lz4/releases/download/v$(LZ4_VERSION)/lz4-$(LZ4_VERSION).tar.gz,$(LZ4_SHA256)))

LZ4_LIBDIR := deps/lz4/lib
LZ4_SRCS := $(call dep_srcs,$(LZ4_LIBDIR),c h)

$(LIBLZ4_SO) : MAKEOVERRIDES=
$(LIBLZ4_SO) : $(LZ4_HEADER) $(LZ4_SRCS)
	$(MAKE) -C $(LZ4_LIBDIR) liblz4
	touch $@

$(LIBLZ4_A) : MAKEOVERRIDES=
$(LIBLZ4_A) : $(LZ4_HEADER) $(LZ4_SRCS)
	$(MAKE) -C $(LZ4_LIBDIR) liblz4.a
	touch $@

# Google Test
GTEST_VERSION ?= 1.17.0
GTEST_SHA256 ?= 65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
$(eval $(call fetch_dependency,googletest,googletest/include/gtest/gtest.h,v$(GTEST_VERSION),https://github.com/google/googletest/releases/download/v$(GTEST_VERSION)/googletest-$(GTEST_VERSION).tar.gz,$(GTEST_SHA256)))

GTEST_SRCS := $(call dep_srcs,$(addprefix deps/googletest/googletest/,src include/gtest include/gtest/internal include/gtest/internal/custom),cc h)

$(LIBGTEST_A) : MAKEOVERRIDES=
$(LIBGTEST_A) : $(GTEST_HEADERS) $(GTEST_SRCS)
	cd deps/googletest && cmake .
	$(MAKE) -C deps/googletest
	touch $@

# build-scripts/cmake/openzl-deps.cmake and CMakeLists.txt must pin the same tarballs
check-dependency-pins: check-cmake-pins
.PHONY: check-cmake-pins
check-cmake-pins:
	@for v in $(ZSTD_VERSION) $(ZSTD_SHA256) $(LZ4_VERSION) $(LZ4_SHA256); do \
	    grep -q "\"$$v\"" build-scripts/cmake/openzl-deps.cmake || \
	    { echo "error: build-scripts/cmake/openzl-deps.cmake lacks \"$$v\"" >&2; exit 1; }; done
	@for v in googletest-$(GTEST_VERSION).tar.gz $(GTEST_SHA256); do \
	    grep -qF "$$v" CMakeLists.txt || { echo "error: CMakeLists.txt lacks $$v" >&2; exit 1; }; done

# XGBoost
XGBOOST_VERSION ?= 3.1.0
XGBOOST_SHA256 ?= 4c42d35976067270a9255bf9ee290a706917bb3929a60cdd74d4dd3f1a9c86cc
$(eval $(call fetch_dependency,xgboost,include/xgboost/c_api.h,v$(XGBOOST_VERSION),https://github.com/dmlc/xgboost/releases/download/v$(XGBOOST_VERSION)/xgboost-src-$(XGBOOST_VERSION).tar.gz,$(XGBOOST_SHA256),--recursive))

XGBOOST_LIBDIR := deps/xgboost/lib

# Platform-specific CMake flags for xgboost
XGBOOST_CMAKE_PLATFORM :=
XGBOOST_LDFLAGS :=
XGBOOST_LDLIBS :=
ifneq (,$(filter $(SMALL_CMD_LINE),$(UNAME)))
    # MinGW/MSYS: Use Unix Makefiles generator and link ws2_32 for Windows socket functions
    XGBOOST_CMAKE_PLATFORM := -G "Unix Makefiles" \
        -DCMAKE_CXX_STANDARD_LIBRARIES="-lws2_32" \
        -DCMAKE_SHARED_LINKER_FLAGS="-lws2_32"
    XGBOOST_LDFLAGS += -L$(abspath $(XGBOOST_LIBDIR))
    XGBOOST_LDLIBS += -lws2_32
endif

XGBOOST_SRCS := $(call dep_srcs,$(addprefix deps/xgboost/,src src/* src/*/* include/xgboost include/xgboost/* dmlc-core/src dmlc-core/src/* dmlc-core/include/dmlc),cc cu cuh h)

$(LIBXGBOOST_A) : MAKEOVERRIDES=
$(LIBXGBOOST_A) : $(XGBOOST_HEADER) $(XGBOOST_SRCS)
	$(MKDIR) -p $(XGBOOST_LIBDIR)
	cd deps/xgboost && mkdir -p build && cd build && cmake .. -DBUILD_STATIC_LIB=ON -DUSE_OPENMP=OFF -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$(abspath $(XGBOOST_LIBDIR)) $(XGBOOST_CMAKE_PLATFORM) && $(MAKE)
	# xgboost's CMakeLists.txt strips the 'lib' prefix on Windows; rename to match expected name
	@if [ -f $(XGBOOST_LIBDIR)/xgboost.a ] && [ ! -f $(XGBOOST_LIBDIR)/libxgboost.a ]; then \
		mv $(XGBOOST_LIBDIR)/xgboost.a $(XGBOOST_LIBDIR)/libxgboost.a; \
	fi
	touch $@

# libdmlc.a is built as part of xgboost static build
$(LIBDMLC_A): $(LIBXGBOOST_A)

# Empty rule: stops make searching implicit rules for each dependency source (~100 failed stat() each)
$(filter-out $(ZSTD_HEADER) $(LZ4_HEADER) $(GTEST_HEADERS) $(XGBOOST_HEADER),$(ZSTD_SRCS) $(LZ4_SRCS) $(GTEST_SRCS) $(XGBOOST_SRCS)): ;

itch:
$(eval $(call cxx_program_shared_o,itch,examples/itch.o $(LIBOBJS),$(LIBZSTD_A) $(LIBLZ4_A)))
