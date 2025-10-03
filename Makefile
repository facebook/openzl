# Copyright (c) Meta Platforms, Inc. and affiliates.

# first recipe is default recipe
.PHONY: default
default : zli

# Common repository-wide definitions
include build/make/zldefs.make

# Provides macros to generate targets
include build/make/multiconf.make

# dependencies
ifneq (,$(filter Windows%,$(OS)))
LIBZSTD_SO := deps/zstd/lib/dll/libzstd.dll
else ifeq ($(shell uname), Darwin)
LIBZSTD_SO := deps/zstd/lib/libzstd.dylib
else
LIBZSTD_SO := deps/zstd/lib/libzstd.so
endif

LIBZSTD_A := deps/zstd/lib/libzstd.a
LIBGTEST_A := deps/googletest/lib/libgtest.a

GTEST_HEADERS := deps/googletest/googletest/include/gtest/gtest.h

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
$(eval $(call static_library,libopenzl.a,$(LIBOBJS),$(LIBZSTD_A)))

libopenzl.so: CFLAGS += -fPIC
$(eval $(call c_dynamic_library,libopenzl.so,$(LIBOBJS),$(LIBZSTD_SO)))

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

zli: CFLAGS += -O3
zli: CXXFLAGS += -O3
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
	$(ZLCPP_OBJS) \
	$(LIBOBJS), \
	$(LIBZSTD_A)))

.PHONY: examples
examples: zs2_pipeline zs2_trygraph zs2_selector zs2_struct zs2_round_trip

.PHONY: test
test : gtests zs2_test
	$(EXEC_PREFIX) ./gtests

.PHONY: zs2_test
zs2_test : examples
	$(EXEC_PREFIX) ./zs2_pipeline
	$(EXEC_PREFIX) ./zs2_trygraph

# ********     Tools     ********

unitBench: CFLAGS += -O3
unitBench: CPPFLAGS += -DNDEBUG
UNITBENCH_COBJS := $(foreach DIR,$(UNITBENCH_DIRS),$(call c_objs,$(DIR)))
$(eval $(call c_program_shared_o,unitBench,tools/time/timefn.o tools/fileio/fileio.o $(UNITBENCH_COBJS) $(LIBOBJS),$(LIBZSTD_A)))

stream_dump2:
$(eval $(call c_program_shared_o,stream_dump2, \
    $(STREAMDUMP_COBJS) tools/fileio/fileio.o $(LIBOBJS),$(LIBZSTD_A)))

$(eval $(call cxx_program,sddl_compiler, \
	$(SDDL_COMPILER_DIR)/main.o \
	$(SDDL_COMPILER_CXXOBJS) \
	$(ZLCPP_OBJS), \
	libopenzl.a \
	$(LIBZSTD_A)))

# Selection of gtest units (by file name convention)
CXX_FILE_OBJS := $(notdir $(CXX_OBJS))
GTEST_FILEO := $(filter test_%.o,$(CXX_FILE_OBJS))
GTEST_FILEO += $(filter %Test.o,$(CXX_FILE_OBJS))
GTEST_FILTER_LIST := VersionTest.o NoIntrospectionTest.o
GTEST_FILEO := $(filter-out $(GTEST_FILTER_LIST),$(GTEST_FILEO))

ALL_TEST_OBJS := $(patsubst %.cpp,%.o,$(foreach dir,$(TESTSDIRS),$(wildcard $(dir)/*.cpp)))
GTEST_OBJS := $(foreach name,$(GTEST_FILEO),$(filter %/$(name),$(ALL_TEST_OBJS)))

# Other module objects used in gtests
DATAGEN_OBJS := \
	tests/datagen/structures/CompressorProducer.o \
	tests/datagen/structures/LocalParamsProducer.o \
	tests/datagen/structures/openzl/StringInputProducer.o \
	tests/datagen/InputExpander.o
CLI_TEST_OBJS := $(filter-out test_%.o,$(notdir $(foreach DIR,$(CLI_TEST_DIRS),$(call cxx_objs,$(DIR)))))
ZLCPP_TEST_OBJS := $(call cxx_objs,$(ZLCPP_TEST_DIR))

ALL_GTESTS_OBJS := \
	tests/gtest_main.o \
	tools/time/timefn.o \
	tests/utils.o \
	tests/local_params_utils.o \
	tests/unittest/common/test_errors_in_c.o \
	tests/compress/ml_selectors/test_zstrong_ml_core_models.o \
	$(GTEST_OBJS) \
	$(ZLCPP_TEST_OBJS) \
	$(CLI_CXXOBJS) \
	$(CLI_TEST_OBJS) \
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
	$(DATAGEN_OBJS) \
	$(ZLCPP_OBJS) \
	$(LIBOBJS)

gtests: $(LIBGTEST_A) $(LIBZSTD_A)
gtests: CPPFLAGS += -Ideps/googletest/googletest/include
gtests: CXXFLAGS += -Wno-undef -Wno-sign-compare
gtests: LDLIBS   += -lpthread
gtests: CPPFLAGS += -g -DZL_ENABLE_ASSERT
$(eval $(call cxx_program,gtests, \
	$(ALL_GTESTS_OBJS), \
	$(LIBGTEST_A) $(LIBZSTD_A)))

# ********     Examples     ********

zs2_pipeline:
$(eval $(call c_program_shared_o,zs2_pipeline,examples/zs2_pipeline.o tools/fileio/fileio.o $(LIBOBJS),$(LIBZSTD_A)))

zs2_struct:
$(eval $(call c_program_shared_o,zs2_struct,examples/zs2_struct.o tools/fileio/fileio.o $(LIBOBJS),$(LIBZSTD_A)))

zs2_trygraph:
$(eval $(call c_program_shared_o,zs2_trygraph,examples/zs2_trygraph.o $(LIBOBJS),$(LIBZSTD_A)))

zs2_selector:
$(eval $(call c_program_shared_o,zs2_selector,examples/zs2_selector.o tools/fileio/fileio.o $(LIBOBJS),$(LIBZSTD_A)))

zs2_round_trip:
$(eval $(call cxx_program_shared_o,zs2_round_trip,tests/round_trip.o tools/fileio/fileio.o $(SHARED_COMPONENTS_CXXOBJS) $(LIBOBJS),$(LIBZSTD_A)))


# ********     Cleaning     ********

.PHONY: clean
clean:
	# note: done by multiconf.make
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
builddeps : $(LIBGTEST_A) $(LIBZSTD_A) $(LIBZSTD_SO)

.PHONY: cleandeps
cleandeps:
	$(RM) -r deps/googletest deps/googletest.tar.gz
	-$(GIT) submodule deinit -f deps/zstd 2> /dev/null
	$(RM) -r deps/zstd deps/$(ZSTD_DIRNAME) $(ZSTD_TARBALL)

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

# Google Test

deps/googletest.tar.gz :
	$(MKDIR) -p deps
	$(CURL) -L https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz -o $@

# Ensure headers are available (no need for compiled library yet)
$(GTEST_HEADERS): deps/googletest.tar.gz
	cd deps && tar xzf googletest.tar.gz
	$(RM) -r deps/googletest
	mv deps/googletest*/ deps/googletest
	touch $@

$(LIBGTEST_A) : MAKEOVERRIDES=
$(LIBGTEST_A) : $(GTEST_HEADERS)
	cd deps/googletest && cmake .
	$(MAKE) -C deps/googletest
