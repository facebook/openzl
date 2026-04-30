# Copyright (c) Meta Platforms, Inc. and affiliates.

# Contain project wide settings
# such as default compilation flags
# build modes
# and paths to source files.

# Enable parallel builds by default (override with ZL_JOBS=N or make -jN)
ZL_JOBS ?= $(shell nproc 2>/dev/null || echo 4)
ifneq ($(filter -j%, $(MAKEFLAGS)),)
# User already specified -j, don't override
else
MAKEFLAGS += -j$(ZL_JOBS)
endif

# save environment variables
USER_CFLAGS := $(CFLAGS)
USER_CXXFLAGS := $(CXXFLAGS)
USER_CPPFLAGS := $(CPPFLAGS)
USER_LDFLAGS := $(LDFLAGS)

# base compilation flags
CPPFLAGS += -I. -Iinclude -Isrc -Icpp/include -Icpp/src
CFLAGS   += -O1 -std=c11  # code must be compliant with C11
CXXFLAGS += -O1 -std=c++1z  # for gtests
DEBUGFLAGS ?= -g \
	-Wall -Wcast-qual -Wcast-align -Wshadow \
	-Wstrict-aliasing=1 -Wundef -Wpointer-arith -Wvla -Wformat=2 \
	-Wfloat-equal -Wswitch-enum -Wimplicit-fallthrough \
	-Wno-unused-function
CDEBUGFLAGS ?= $(DEBUGFLAGS) -Wstrict-prototypes -Wmissing-prototypes -Wredundant-decls -Wconversion -Wextra -Wno-missing-field-initializers
CXXDEBUGFLAGS ?= $(DEBUGFLAGS)
CFLAGS   += $(CDEBUGFLAGS) $(MOREFLAGS)
CXXFLAGS += $(CXXDEBUGFLAGS) $(MOREFLAGS)
LDFLAGS  += $(MOREFLAGS)
LDLIBS   += -lm # note: to be removed from library once dependency fixed
CPPFLAGS += -Ideps/zstd/lib/ # "zstd.h"
CPPFLAGS += -Ideps/lz4/lib/  # "lz4.h"
ARFLAGS  += -c # do not print warning message when creating the archive (expected)

# Default build mode
# support both BUILD_TYPE and BUILD_MODE, priority to BUILD_TYPE
# ADAPTIVE mode provides smart per-target defaults (targets add their own flags)
BUILD_MODE ?= ADAPTIVE
BUILD_TYPE ?= $(BUILD_MODE)

# Sanitizer flags
SANITIZER_FLAGS = -fsanitize=address -fsanitize-address-use-after-scope -fsanitize=undefined -fno-omit-frame-pointer

# Log level
LOG_LEVEL ?= SEQ

# Build mode configuration
ifeq ($(BUILD_TYPE),DEV)
    CFLAGS += -g -O0
    CXXFLAGS += -g -O0
    CPPFLAGS += -DZL_ENABLE_ASSERT
    CFLAGS += $(SANITIZER_FLAGS)
    CXXFLAGS += $(SANITIZER_FLAGS)
    LDFLAGS += $(SANITIZER_FLAGS)
else ifeq ($(BUILD_TYPE),DEV_NOSAN)
    CFLAGS += -g -O0
    CXXFLAGS += -g -O0
    CPPFLAGS += -DZL_ENABLE_ASSERT
else ifeq ($(BUILD_TYPE),TRACES)
    CFLAGS += -g -O0
    CXXFLAGS += -g -O0
    CPPFLAGS += -DZL_ENABLE_ASSERT -DZL_LOG_LVL=ZL_LOG_LVL_$(LOG_LEVEL)
    CFLAGS += $(SANITIZER_FLAGS)
    CXXFLAGS += $(SANITIZER_FLAGS)
    LDFLAGS += $(SANITIZER_FLAGS)
else ifeq ($(BUILD_TYPE),TRACES_NOSAN)
    CFLAGS += -g -O0
    CXXFLAGS += -g -O0
    CPPFLAGS += -DZL_ENABLE_ASSERT -DZL_LOG_LVL=ZL_LOG_LVL_$(LOG_LEVEL)
else ifeq ($(BUILD_TYPE),OPT)
    CDEBUGFLAGS =
    DEBUGFLAGS =
    CFLAGS += -g0 -O3
    CXXFLAGS += -g0 -O3
    CPPFLAGS += -DNDEBUG
    MCM_STRIP ?= 1
else ifeq ($(BUILD_TYPE),OPT_ASAN)
    CFLAGS += -O3 -DNDEBUG
    CXXFLAGS += -O3 -DNDEBUG
    CPPFLAGS += -DNDEBUG
    CFLAGS += $(SANITIZER_FLAGS)
    CXXFLAGS += $(SANITIZER_FLAGS)
    LDFLAGS += $(SANITIZER_FLAGS)
else ifeq ($(BUILD_TYPE),DBGO)
    CFLAGS += -g -O2
    CXXFLAGS += -g -O2
    CPPFLAGS += -DZL_ENABLE_ASSERT
else ifeq ($(BUILD_TYPE),DBGO_ASAN)
    CFLAGS += -g -O2
    CXXFLAGS += -g -O2
    CPPFLAGS += -DZL_ENABLE_ASSERT
    CFLAGS += $(SANITIZER_FLAGS)
    CXXFLAGS += $(SANITIZER_FLAGS)
    LDFLAGS += $(SANITIZER_FLAGS)
else ifeq ($(BUILD_TYPE),BASELINE)
# no modification, just use baseline flags
else ifeq ($(BUILD_TYPE),ADAPTIVE)
# ADAPTIVE mode: baseline flags, targets add their own optimizations
else
    $(error Invalid BUILD_TYPE: $(BUILD_TYPE). Valid options: ADAPTIVE, OPT, BASELINE, OPT_ASAN, DBGO, DBGO_ASAN, DEV, DEV_NOSAN, TRACES, TRACES_NOSAN)
endif

# position user flags at the end, so that they have higher priority
CFLAGS += $(USER_CFLAGS)
CXXFLAGS += $(USER_CXXFLAGS)
CPPFLAGS += $(USER_CPPFLAGS)
LDFLAGS += $(USER_LDFLAGS)

# Show current build configuration
.PHONY: show-config
show-config:
	@echo "Build Type: $(BUILD_TYPE)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "CXXFLAGS: $(CXXFLAGS)"
	@echo "CPPFLAGS: $(CPPFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"

.PHONY: help
help:
	@echo "Available build types:"
	@echo "  BUILD_TYPE=ADAPTIVE     - (default) Smart per-target defaults"
	@echo "                            Production: -O3 -DNDEBUG"
	@echo "                            Tests: -g -DZL_ENABLE_ASSERT"
	@echo "                            Benchmarks: -O3 -DNDEBUG"
	@echo "  BUILD_TYPE=OPT          - Optimized build, no asserts (coercive for all targets)"
	@echo "  BUILD_TYPE=DEV          - Debug build with asserts and sanitizers (coercive)"
	@echo "  BUILD_TYPE=DEV_NOSAN    - Debug build with asserts (no sanitizers)"
	@echo "  BUILD_TYPE=OPT_ASAN     - Optimized build with sanitizers"
	@echo "  BUILD_TYPE=DBGO         - Optimized build with asserts"
	@echo "  BUILD_TYPE=DBGO_ASAN    - Optimized build with asserts and sanitizers"
	@echo "  BUILD_TYPE=TRACES       - Debug build with asserts, sanitizers and traces"
	@echo "  BUILD_TYPE=TRACES_NOSAN - Debug build with asserts and traces"
	@echo ""
	@echo "Usage examples:"
	@echo "  make                            # Build with ADAPTIVE mode (smart defaults)"
	@echo "  make zli                        # Build zli with -O3 -DNDEBUG (ADAPTIVE)"
	@echo "  make gtests                     # Build gtests with -g -DZL_ENABLE_ASSERT (ADAPTIVE)"
	@echo "  make BUILD_TYPE=OPT             # Force OPT mode for all targets"
	@echo "  make BUILD_TYPE=DEV             # Force DEV mode for all targets"
	@echo "  make show-config BUILD_TYPE=DEV # Show configuration for DEV type"

# Paths to source files
EXROOT  := examples
EXDIRS  := $(shell find $(EXROOT) -type d)
LIBROOT := src
LIBDIRS := $(shell find $(LIBROOT) -type d)
TESTROOT:= tests
TESTSDIRS:= $(shell find $(TESTROOT) -type d)
CLIDIR  := cli
CLIDIRS := $(CLIDIR) $(CLIDIR)/args $(CLIDIR)/commands $(CLIDIR)/utils
CLI_TEST_DIRS := $(CLIDIR)/tests $(CLIDIR)/training/tests
CLI_DIRS := $(CLIDIRS) $(CLI_TEST_DIRS)
ARGDIR  := tools/arg
FILEIODIR := tools/fileio
IODIR := tools/io
LOGGERDIR := tools/logger
TIMEDIR := tools/time
STREAMDUMPDIR := tools/streamdump
UNITBENCH_ROOT:= benchmark/unitBench
UNITBENCH_DIRS:= $(shell find $(UNITBENCH_ROOT) -type d)
CUSTOMPARSERSDIR := custom_parsers
CSVDIR := $(CUSTOMPARSERSDIR)/csv
PROFILES_SDDL_DIR := $(CUSTOMPARSERSDIR)/sddl
PARQUETDIR := $(CUSTOMPARSERSDIR)/parquet
SHARED_COMPONENTSDIR := $(CUSTOMPARSERSDIR)/shared_components
VISUALIZER_CPPDIR := tools/zl_visualizer/compression_introspection
ZLCPP_ROOT := cpp/src
ZLCPP_DIRS := $(shell find $(ZLCPP_ROOT) -type d)
ZLCPP_TEST_DIR := cpp/tests
TRAINING_ROOTS := tools/training/clustering tools/training/ace tools/training/graph_mutation tools/training/utils tools/training/sample_collection
TRAINING_DIRS := tools/training $(shell find $(TRAINING_ROOTS) -type d)
TRAINING_TEST_DIRS := $(shell find tools/training/tests -type d)
SDDL_COMPILER_DIR := tools/sddl/compiler
SDDL_COMPILER_TESTS_DIR := $(SDDL_COMPILER_DIR)/tests
SDDL2_COMPILER_DIR := tools/sddl2/compiler
SDDL2_COMPILER_DIRS := $(filter-out %/tests,$(shell find $(SDDL2_COMPILER_DIR) -type d))
SDDL2_COMPILER_TESTS_DIR := $(SDDL2_COMPILER_DIR)/tests
SDDL2_ASSEMBLER_DIR := tools/sddl2/assembler
SDDL2_ASSEMBLER_TESTS_DIR := $(SDDL2_ASSEMBLER_DIR)/tests
ML_SELECTOR_DIR := tools/ml_selector
ML_SELECTOR_TESTS_DIR := $(ML_SELECTOR_DIR)/tests
TEST_REGISTRY_DIRS := tests/registry tests/registry/components

# input for multiconf.make
C_SRCDIRS   := $(LIBDIRS) $(EXDIRS) $(TESTSDIRS) $(FILEIODIR) $(TIMEDIR) $(STREAMDUMPDIR) $(UNITBENCH_DIRS) $(CUSTOMPARSERSDIR) $(CSVDIR) $(PROFILES_SDDL_DIR) $(PARQUETDIR) $(CLI_DIRS) $(ML_SELECTOR_DIR)
ASM_SRCDIRS := $(LIBDIRS)
CXX_SRCDIRS := \
	$(CLIDIRS) \
	$(ARGDIR) \
	$(TESTSDIRS) \
	$(FILEIODIR) \
	$(TIMEDIR) \
	$(LOGGERDIR) \
	$(CUSTOMPARSERSDIR) \
	$(CSVDIR) \
	$(PROFILES_SDDL_DIR) \
	$(PARQUETDIR) \
	$(SHARED_COMPONENTSDIR) \
	$(VISUALIZER_CPPDIR) \
	$(IODIR) \
	$(ZLCPP_DIRS) \
	$(ZLCPP_TEST_DIR) \
	$(TRAINING_DIRS) \
	$(TRAINING_TEST_DIRS) \
	$(SDDL_COMPILER_DIR) \
	$(SDDL_COMPILER_TESTS_DIR) \
	$(SDDL2_COMPILER_DIRS) \
	$(SDDL2_COMPILER_TESTS_DIR) \
	$(SDDL2_ASSEMBLER_DIR) \
	$(SDDL2_ASSEMBLER_TESTS_DIR) \
	$(CLI_DIRS) \
	$(CLI_TEST_DIRS) \
	$(ML_SELECTOR_DIR) \
	$(ML_SELECTOR_TESTS_DIR) \
	$(TEST_REGISTRY_DIRS)

# Use response file for link stage for environments with small command line limit (<= 32KB)
UNAME := $(shell sh -c 'MSYSTEM="MSYS" uname')
SMALL_CMD_LINE ?= MSYS_NT% CYGWIN_NT%
ifneq (,$(filter $(SMALL_CMD_LINE),$(UNAME)))
MCM_LD_RESPONSE_FILE := 1
endif

# Set executable suffix for Windows/MinGW environments
EXE :=
ifneq (,$(filter $(SMALL_CMD_LINE),$(UNAME)))
EXE := .exe
endif
