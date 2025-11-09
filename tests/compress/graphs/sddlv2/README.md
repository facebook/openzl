# SDDL2 Test Suite

## Quick Start

```bash
# Run all tests
make test

# Run specific test
./sddl2_vm_test
./sddl2_interpreter_test

# Clean build artifacts
make clean
```

## Test Organization

```
tests/compress/graphs/sddlv2/
├── README.md                    (this file)
├── Makefile                     (simple build)
│
├── sddl2_vm_test.c              (Stack operations)
├── sddl2_arithmetic_test.c      (Arithmetic ops)
├── sddl2_input_test.c           (Input buffer)
├── sddl2_segments_test.c        (Segments)
├── sddl2_tagged_segments_test.c (Tagged segments)
├── sddl2_interpreter_test.c     (Bytecode interpreter)
├── sddl2_assembly_test.c        (End-to-end)
│
├── asm/                         (Assembly test sources)
│   ├── test_math_add.asm
│   ├── test_math_combined.asm
│   ├── test_stack_drop.asm
│   └── test_cmp_all.asm
│
├── generate_test_bytecode.py   (Generator script)
└── generated_test_bytecode.h   (Auto-generated, do not edit)
```

## Interpreter Tests from Assembly

Some interpreter tests use assembly sources that are auto-converted to bytecode.

**Regenerate when opcodes change:**
```bash
python3 generate_test_bytecode.py
```

**Add new test:**
1. Create `asm/test_name.asm` with assembly code
2. Run `python3 generate_test_bytecode.py`
3. Use `BYTECODE_TEST_NAME` constant in C test:
   ```c
   #include "generated_test_bytecode.h"

   static void test_name(void) {
       const uint8_t* bytecode = BYTECODE_TEST_NAME;
       size_t size = BYTECODE_TEST_NAME_SIZE;
       // ... execute and assert
   }
   ```

## Adding Tests

### Standard C Test

1. Write test function in appropriate file:
   ```c
   static void test_my_feature(void) {
       SDDL2_stack* stack = create_test_stack(100);

       assert(SDDL2_op_my_feature(stack) == SDDL2_OK);
       assert(/* verify result */);

       destroy_test_stack(stack);
       printf("✓ test_my_feature passed\n");
   }
   ```

2. Register in `main()`:
   ```c
   int main(void) {
       test_existing();
       test_my_feature();  // Add here
       return 0;
   }
   ```

3. Run: `make test`

### Conventions

- Use `assert()` for critical checks
- Descriptive names: `test_<feature>_<scenario>`
- Print success: `printf("✓ test_name passed\n")`
- Clean up resources
- Keep tests isolated

## Why Standalone C Tests?

These tests use plain C (not gtest) because:
- **Zero dependencies**: Matches VM's architecture
- **Portable**: Runs anywhere with a C compiler
- **Simple**: No framework, just `make` and run
- **Fast**: Compile and execute in <1 second

For higher-level OpenZL integration tests, gtest is appropriate. These are VM unit tests.
