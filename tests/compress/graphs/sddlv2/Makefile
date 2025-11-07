# Makefile for OpenZL VM (SDDL2) Tests
# Tests Phases 1-5 of the OpenZL Execution Engine

# Compiler and flags
CC ?= gcc
CFLAGS += -O1 -std=c11 -g -Wall
INCLUDES = -I../../../.. -I../../../../src
CPPFLAGS += $(INCLUDES)

# Source files
VM_SRC = ../../../../src/openzl/compress/graphs/sddlv2/sddl2_vm.c

# Test executables
TESTS = sddl2_vm_test \
        sddl2_arithmetic_test \
        sddl2_input_test \
        sddl2_segments_test \
        sddl2_tagged_segments_test

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
	@echo "=== Phase 1: Foundation (Stack + Values) ==="
	@./sddl2_vm_test
	@echo ""
	@echo "=== Phase 2: Arithmetic Operations ==="
	@./sddl2_arithmetic_test
	@echo ""
	@echo "=== Phase 3: Input Buffer ==="
	@./sddl2_input_test
	@echo ""
	@echo "=== Phase 4: Unspecified Segments ==="
	@./sddl2_segments_test
	@echo ""
	@echo "=== Phase 5: Tagged Segments with Merging ==="
	@./sddl2_tagged_segments_test
	@echo ""
	@echo "=========================================="
	@echo "✅ All tests completed successfully!"
	@echo "=========================================="

# Individual test targets
sddl2_vm_test: sddl2_vm_test.c $(VM_SRC)

sddl2_arithmetic_test: sddl2_arithmetic_test.c $(VM_SRC)

sddl2_input_test: sddl2_input_test.c $(VM_SRC)

sddl2_segments_test: sddl2_segments_test.c $(VM_SRC)

sddl2_tagged_segments_test: sddl2_tagged_segments_test.c $(VM_SRC)

# Build all tests without running
.PHONY: build
build: $(TESTS)
	@echo "All tests built successfully"

# Run individual test suites
.PHONY: test-phase1
test-phase1: sddl2_vm_test
	@echo "=== Phase 1: Foundation ==="
	@./sddl2_vm_test

.PHONY: test-phase2
test-phase2: sddl2_arithmetic_test
	@echo "=== Phase 2: Arithmetic ==="
	@./sddl2_arithmetic_test

.PHONY: test-phase3
test-phase3: sddl2_input_test
	@echo "=== Phase 3: Input Buffer ==="
	@./sddl2_input_test

.PHONY: test-phase4
test-phase4: sddl2_segments_test
	@echo "=== Phase 4: Segments ==="
	@./sddl2_segments_test

.PHONY: test-phase5
test-phase5: sddl2_tagged_segments_test
	@echo "=== Phase 5: Tagged Segments ==="
	@./sddl2_tagged_segments_test

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
	@echo "Individual phase tests:"
	@echo "  make test-phase1  - Run Phase 1 tests (Foundation)"
	@echo "  make test-phase2  - Run Phase 2 tests (Arithmetic)"
	@echo "  make test-phase3  - Run Phase 3 tests (Input Buffer)"
	@echo "  make test-phase4  - Run Phase 4 tests (Segments)"
	@echo "  make test-phase5  - Run Phase 5 tests (Tagged Segments)"
	@echo ""
	@echo "Individual test executables:"
	@echo "  make sddl2_vm_test"
	@echo "  make sddl2_arithmetic_test"
	@echo "  make sddl2_input_test"
	@echo "  make sddl2_segments_test"
	@echo "  make sddl2_tagged_segments_test"
