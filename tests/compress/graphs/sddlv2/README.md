# OpenZL VM (SDDL2) Tests

This directory contains the test suite for the OpenZL Execution Engine VM implementation.

## Quick Start

Run all tests:
```bash
make
```

Or:
```bash
make test
```

## Test Organization

The test suite is organized by implementation phases:

| Phase | Test File | Description | Test Count |
|-------|-----------|-------------|------------|
| 1 | `sddl2_vm_test.c` | Foundation (Stack + Values) | 7 |
| 2 | `sddl2_arithmetic_test.c` | Arithmetic Operations | 25 |
| 3 | `sddl2_input_test.c` | Input Buffer Operations | 18 |
| 4 | `sddl2_segments_test.c` | Unspecified Segments | 20 |
| 5 | `sddl2_tagged_segments_test.c` | Tagged Segments with Merging | 20 |

**Total: 90 tests**

## Makefile Targets

### Main Targets

- `make` or `make test` - Build and run all tests
- `make build` - Build all tests without running them
- `make clean` - Remove all test executables
- `make help` - Display help information

### Individual Phase Tests

Run tests for a specific phase:

```bash
make test-phase1  # Foundation
make test-phase2  # Arithmetic
make test-phase3  # Input Buffer
make test-phase4  # Segments
make test-phase5  # Tagged Segments
```

### Individual Test Executables

Build a specific test:

```bash
make sddl2_vm_test
make sddl2_arithmetic_test
make sddl2_input_test
make sddl2_segments_test
make sddl2_tagged_segments_test
```

## Test Details

### Phase 1: Foundation (7 tests)
- Stack initialization and operations
- Push/pop with bounds checking
- Stack overflow/underflow handling
- Value kinds (I64, Tag, Type)
- Type size calculations

### Phase 2: Arithmetic (25 tests)
- Basic operations: add, sub, mul, div, mod, abs, neg
- Overflow detection for all operations
- Divide-by-zero handling
- Type mismatch detection
- Special cases (INT64_MIN, zero operations)

### Phase 3: Input Buffer (18 tests)
- Buffer initialization
- Current position operations
- Byte loading with bounds checking
- Random access (no cursor advancement)
- Integration with arithmetic

### Phase 4: Unspecified Segments (20 tests)
- Segment list management
- Single and multiple segment creation
- Zero-size segments
- Dynamic list growth (50+ segments)
- Bounds checking
- Integration with arithmetic

### Phase 5: Tagged Segments with Merging (20 tests)
- Tag registry management
- Tagged segment creation with type information
- Automatic segment merging (same tag + same type + consecutive)
- No merging when tags/types differ
- Error handling (negative values, bounds, type mismatches)
- Registry growth with many tags

## Compilation Details

The Makefile uses:
- **Compiler**: `gcc`
- **C Standard**: C11
- **Optimization**: `-O1`
- **Debug Info**: `-g`
- **Warnings**: `-Wall`

**Note**: The VM does not require `-lm` (math library). All arithmetic operations are implemented without external math functions.

## Expected Output

When running `make test`, you should see:

```
==========================================
Running OpenZL VM Test Suite
==========================================

=== Phase 1: Foundation (Stack + Values) ===
✓ test_stack_init passed
✓ test_stack_push_pop passed
...
✓✓✓ All Phase 1 tests passed! ✓✓✓

=== Phase 2: Arithmetic Operations ===
...

==========================================
✅ All tests completed successfully!
==========================================
```

## Troubleshooting

### Test Fails to Compile

Make sure you're in the correct directory:
```bash
cd tests/compress/graphs/sddlv2
```

### Cannot Find VM Source

The Makefile expects the VM source at:
```
../../../../src/openzl/compress/graphs/sddlv2/sddl2_vm.c
```

If your directory structure is different, adjust the `VM_SRC` variable in the Makefile.

### Permission Issues

Make sure the test executables are executable:
```bash
chmod +x sddl2_*_test
```

## Development Workflow

1. **Make changes** to VM implementation (`sddl2_vm.c` or `sddl2_vm.h`)
2. **Run tests** to verify: `make test`
3. **Debug specific phase** if needed: `make test-phase2`
4. **Clean up** when done: `make clean`

## Adding New Tests

To add a new test file:

1. Create test file: `sddl2_newfeature_test.c`
2. Add to `TESTS` variable in Makefile
3. Add build target following the pattern:
   ```makefile
   sddl2_newfeature_test: sddl2_newfeature_test.c $(VM_SRC)
       $(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)
   ```
4. Add to the `test` target to run automatically

## Related Documentation

- `/src/openzl/compress/graphs/sddlv2/IMPLEMENTATION.md` - Implementation plan and progress
- `/src/openzl/compress/graphs/sddlv2/sddl2_vm.h` - VM interface documentation

## Status

**Current Status**: Phases 1-5 Complete ✅

- ✅ Phase 1: Foundation
- ✅ Phase 2: Arithmetic
- ✅ Phase 3: Input Buffer
- ✅ Phase 4: Unspecified Segments
- ✅ Phase 5: Tagged Segments with Merging

**Milestone**: End-to-end segment generation working with typed, tagged segments and automatic merging!
