# Makefile for OpenZL VM (SDDL2) Tests
# Tests Phases 1-5 of the OpenZL Execution Engine

# Compiler and flags
CC ?= gcc
CFLAGS += -O1 -std=c11 -g -Wall -Wswitch-enum -Werror -Wextra -DSDDL2_ENABLE_TEST_ALLOCATOR
INCLUDES = -I../../../.. -I../../../../src -I../../../../include
CPPFLAGS += $(INCLUDES)

# Source files
VM_SRC = ../../../../src/openzl/compress/graphs/sddl2/sddl2_vm.c
INTERPRETER_SRC = ../../../../src/openzl/compress/graphs/sddl2/sddl2_interpreter.c
LOGGING_SRC = ../../../../src/openzl/common/logging.c

# Test executables
TESTS = sddl2_vm_test \
        sddl2_arithmetic_test \
        sddl2_input_test \
        sddl2_segments_test \
        sddl2_tagged_segments_test \
        sddl2_type_size_test \
        sddl2_type_fixed_array_test \
        sddl2_type_structure_test \
        sddl2_vm_type_structure_test \
        sddl2_structure_segment_test \
        sddl2_field_extraction_test \
        sddl2_structure_split_integration_test \
        sddl2_load_test \
        sddl2_interpreter_test \
        sddl2_bytecode_execution_test

# Default target: run all tests
.PHONY: all
all: test

# Build and run all tests
.PHONY: test
test: $(TESTS)
	@echo "=========================================="
	@echo "Running OpenZL VM Test Suite"
	@echo "=========================================="
	@echo ""
	@for test in $(TESTS); do \
		echo "Running $$test..."; \
		./$$test || exit 1; \
		echo ""; \
	done
	@echo "=========================================="
	@echo "✅ All tests completed successfully!"
	@echo "=========================================="

# Individual test targets
sddl2_vm_test: sddl2_vm_test.c $(VM_SRC)

sddl2_arithmetic_test: sddl2_arithmetic_test.c $(VM_SRC)

sddl2_input_test: sddl2_input_test.c $(VM_SRC)

sddl2_segments_test: sddl2_segments_test.c $(VM_SRC)

sddl2_tagged_segments_test: sddl2_tagged_segments_test.c $(VM_SRC)

sddl2_type_size_test: sddl2_type_size_test.c $(VM_SRC)

sddl2_type_fixed_array_test: sddl2_type_fixed_array_test.c $(VM_SRC)

sddl2_type_structure_test: sddl2_type_structure_test.c $(VM_SRC)

sddl2_vm_type_structure_test: sddl2_vm_type_structure_test.c $(VM_SRC)

sddl2_structure_segment_test: sddl2_structure_segment_test.c $(VM_SRC)

sddl2_field_extraction_test: sddl2_field_extraction_test.c $(VM_SRC)

sddl2_structure_split_integration_test: sddl2_structure_split_integration_test.c $(VM_SRC)

sddl2_load_test: sddl2_load_test.c $(VM_SRC)

sddl2_interpreter_test: sddl2_interpreter_test.c $(VM_SRC) $(INTERPRETER_SRC) $(LOGGING_SRC)

sddl2_bytecode_execution_test: sddl2_bytecode_execution_test.c $(VM_SRC) $(INTERPRETER_SRC) $(LOGGING_SRC)

# Build all tests without running
.PHONY: build
build: $(TESTS)
	@echo "All tests built successfully"

# Clean up built executables
.PHONY: clean
clean:
	rm -f $(TESTS)
	@echo "Cleaned test executables"

# Help target
.PHONY: help
help:
	@echo "OpenZL VM Test Suite Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  make           - Build and run all tests (default)"
	@echo "  make test      - Build and run all tests"
	@echo "  make build     - Build all tests without running"
	@echo "  make clean     - Remove all test executables"
	@echo ""
	@echo "Build individual tests:"
	@for test in $(TESTS); do \
		echo "  make $$test"; \
	done
