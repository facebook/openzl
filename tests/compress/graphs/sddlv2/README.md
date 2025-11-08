# SDDL2 VM Test Suite

## Overview

The SDDL2 VM test suite uses **standalone C tests** with a simple Makefile-based build system, diverging from OpenZL's current gtest infrastructure. This document explains the rationale behind this architectural decision.

## Quick Start

```bash
# Run all tests
make test

# Run specific test
./sddl2_vm_test
./sddl2_arithmetic_test
./sddl2_interpreter_test

# Clean build artifacts
make clean
```

All tests should pass.

---

## Rationale for Standalone C Tests

### 1. **Architectural Consistency: Zero Dependencies**

The SDDL2 VM is explicitly designed with **zero external dependencies**:

```c
// Production code dependencies (sddl2_vm.h):
#include <stddef.h>   // Only for size_t, NULL
#include <stdint.h>   // Only for int64_t, uint32_t

// Conditional test support (behind #ifdef):
#ifdef SDDL2_ENABLE_TEST_ALLOCATOR
#include <stdlib.h>   // Only in test builds
#endif
```

Significant efforts were made to eliminate even standard library dependencies (see commits eliminating `<stdlib.h>` and `<string.h>` from production builds).
Requiring C++ and gtest contradicts this principle.

**Design Philosophy**: The VM tests reflect the VM's architecture—minimal, portable, autonomous.

---

### 2. **Portability: Test Where It Runs**

The SDDL2 VM is designed to be embeddable in environments where:
- C++ compilers may not be available
- Standard Template Library (STL) is too heavy
- Build infrastructure is minimal
- Cross-compilation to exotic architectures is required

**Standalone C tests enable**:
```bash
# Test on embedded ARM target
arm-none-eabi-gcc sddl2_vm_test.c sddl2_vm.c -o test.elf
qemu-arm test.elf

# Test on RISC-V
riscv64-linux-gnu-gcc sddl2_vm_test.c sddl2_vm.c -o test
./test
```

With gtest, this becomes **impossible** without porting the entire C++ framework and its dependencies.

---

### 3. **Simplicity and Transparency**

SDDL2 VM tests validate low-level bytecode interpreter behavior. The test structure matches this simplicity:

```c
// Clear, direct, no hidden machinery
static void test_div_by_zero(void) {
    SDDL2_stack* stack = create_test_stack(100);

    assert(SDDL2_stack_push(stack, SDDL2_value_i64(10)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_div(stack) == SDDL2_DIV_ZERO);

    destroy_test_stack(stack);
    printf("✓ test_div_by_zero passed\n");
}

int main(void) {
    test_div_by_zero();
    // ... more tests
    return 0;
}
```

**Benefits**:
- Single file contains the entire test
- `main()` is visible—clear execution order
- No fixture inheritance or macro expansion
- Debugger shows exact call stack
- New contributors understand tests in minutes

Compare with gtest equivalent:
```cpp
class SDDL2Test : public ::testing::Test {
  protected:
    void SetUp() override { stack = create_test_stack(100); }
    void TearDown() override { destroy_test_stack(stack); }
    SDDL2_stack* stack;
};

TEST_F(SDDL2Test, DivByZero) {
    ASSERT_EQ(SDDL2_stack_push(stack, SDDL2_value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_stack_push(stack, SDDL2_value_i64(0)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_div(stack), SDDL2_DIV_ZERO);
}
```

More abstraction, hidden control flow, requires understanding gtest machinery.

---

### 4. **Development Independence**

The SDDL2 VM was developed iteratively with tests guiding implementation (TDD-style). Standalone tests provided:

- **Fast iteration**: Compile and run in <1 second
- **No build system setup**: Just `make`
- **Easy extraction**: Copy 2-3 files, compile, done
- **Debugging simplicity**: Standard C debugger workflow

This enabled rapid prototyping during the VM's development phase.

---

### 5. **Precedent in C Codebases**

Major C projects use C tests for similar reasons:

| Project | Test Strategy | Rationale |
|---------|---------------|-----------|
| **Linux kernel** | C tests (kselftest) | Portability, kernel context |
| **SQLite** | C tests + TCL harness | Simplicity, extensive coverage |
| **Redis** | C tests + TCL scripts | Performance testing |
| **Zstd** | C tests + specialized tools | Core library simplicity |
| **Git** | C tests + shell scripts | POSIX portability |

These projects recognize that **C code tests best in C**.

---

## Acknowledged Trade-offs

### What gtest offers:

1. **Rich Assertion Messages**
   - Current: `Assertion failed: (result == SDDL2_OK)`
   - gtest: Shows expected vs. actual, custom messages
   - **Mitigation**: We use descriptive variable names and printf debugging

2. **Test Discovery**
   - Current: Manual test registration in `main()`
   - gtest: Automatic test discovery
   - **Mitigation**: Simple convention—add test function to main()

3. **Filtering and Parameterization**
   - Current: Run all tests or modify `main()` temporarily
   - gtest: `--gtest_filter=SDDL2_*`
   - **Mitigation**: Small test suite (87 tests run in <0.1s), minimal need

4. **CI Integration**
   - Current: Not integrated with Meta's gtest infrastructure
   - gtest: Automatic reporting, metrics, retry
   - **Mitigation**: Return code 0/1 works with any CI system

5. **Team Familiarity**
   - Current: Some engineers may be unfamiliar with non-gtest tests
   - gtest: Everyone at Meta knows it
   - **Mitigation**: Tests are simple C, easy to understand

---

## When to reconsider?

Migrate to gtest when:

1. **SDDL2 grows significantly** (10x more tests, complex scenarios)
2. **Integration testing needs** exceed unit testing (testing OpenZL graph integration)
3. **Team consensus** strongly favors uniformity over the benefits listed above
4. **CI requirements** mandate gtest (organizational policy)

For now, the VM's architecture and test requirements favor standalone C tests.

---

## Comparison

| Test Type | Best Tool | Reason |
|-----------|-----------|--------|
| **VM unit tests** | **Standalone C** | Simple, fast, portable |
| **OpenZL integration** | **gtest** | Rich assertions, fixtures for graph setup |
| **Fuzz testing** | **libFuzzer** | Specialized tool for the job |
| **End-to-end** | **Scripts** | High-level validation |

Let's use the right tool for each job.

---

## Test Organization

```
tests/compress/graphs/sddlv2/
├── README.md                    (this file)
├── Makefile                     (simple build system)
│
├── sddl2_vm_test.c              (Phase 1: Stack operations)
├── sddl2_arithmetic_test.c      (Phase 2: Arithmetic ops)
├── sddl2_input_test.c           (Phase 3: Input buffer)
├── sddl2_segments_test.c        (Phase 4: Segments)
├── sddl2_tagged_segments_test.c (Phase 5: Tagged segments)
├── sddl2_interpreter_test.c     (Bytecode interpreter)
└── sddl2_assembly_test.c        (End-to-end pipeline)
```

Each test file is self-contained and can be run independently.

---

## Contributing

### Adding a New Test

1. **Choose appropriate test file** (or create new one for new feature area)
2. **Write test function**:
   ```c
   static void test_my_feature(void) {
       // Setup
       SDDL2_stack* stack = create_test_stack(100);

       // Exercise
       SDDL2_error err = SDDL2_op_my_feature(stack);

       // Verify
       assert(err == SDDL2_OK);
       assert(/* check result */);

       // Cleanup
       destroy_test_stack(stack);
       printf("✓ test_my_feature passed\n");
   }
   ```
3. **Register in main()**:
   ```c
   int main(void) {
       test_existing_1();
       test_existing_2();
       test_my_feature();  // Add here
       return 0;
   }
   ```
4. **Run**: `make test`

### Test Conventions

- Use `assert()` for critical checks (test fails on violation)
- Use descriptive function names: `test_<feature>_<scenario>`
- Print success message: `printf("✓ test_name passed\n")`
- Clean up resources in every test
- Keep tests isolated (no shared state)

---

## FAQ

**Q: Why not just use `EXPECT_NO_THROW()` wrapper around C tests?**

A: That loses the benefits of C tests (portability, simplicity) while gaining minimal benefit.

**Q: Doesn't this create a "test island"?**

A: Yes. The SDDL2 VM is a self-contained bytecode interpreter. Its tests reflect this architecture.

**Q: What if I want to use gtest features for SDDL2 integration tests?**

A: Perfect! Create integration tests in `tests/compress/graphs/` using gtest. Keep VM unit tests in C.

---

## Summary

SDDL2 VM uses standalone C tests because:

1. **Consistency**: VM has zero dependencies; tests match this
2. **Portability**: Can test on any platform with C compiler
3. **Simplicity**: Low-level code deserves low-level tests
4. **Independence**: Enables isolated development and debugging
5. **Precedent**: Industry-standard approach for C libraries

This is **not** a rejection of gtest—it's choosing the appropriate tool for this specific component.
`gtest` is welcome for higher-level integration testing while keeping unit tests simple and portable.
