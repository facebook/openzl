# SDDL2 Test Suite

## Test Organization

The tests are split into two layers:

### Assembly Execution Tests (`test_sddl2_assembly_execution.cpp`)

End-to-end tests that exercise the full pipeline: assembly source → assembler
→ bytecode → VM execution → segment output. Each test case contains an inline
assembly program as a raw string literal, assembles it, executes the resulting
bytecode against an input buffer, and verifies the output segments and error
codes.

### VM Unit Tests (`test_sddl2_*.cpp`)

Lower-level tests that exercise individual VM operations directly through the
C API (e.g. `SDDL2_op_add`, `SDDL2_Stack_push`, `SDDL2_op_expect_true`).
These set up the stack and input cursor manually, call a single operation, and
verify the resulting stack state, error codes, and side effects. They use a
shared test fixture defined in `utils.h`.

| File | What it covers |
|------|----------------|
| `test_sddl2_vm.cpp` | Stack push/pop, value types |
| `test_sddl2_arithmetic.cpp` | Math operations (add, sub, mul, div, mod, abs, neg) |
| `test_sddl2_logic.cpp` | Bitwise logic (and, or, xor, not) |
| `test_sddl2_segments.cpp` | Unspecified segment creation, bounds checking |
| `test_sddl2_tagged_segments.cpp` | Tagged segment creation with types |
| `test_sddl2_input.cpp` | Input cursor management |
| `test_sddl2_expect.cpp` | `expect_true` validation operation |
| `test_sddl2_stack_depth.cpp` | `push.stack_depth` introspection |
| `test_sddl2_stack_drop_if.cpp` | Conditional stack drop |
| `test_sddl2_type_fixed_array.cpp` | `type.fixed_array` composite type |
| `test_sddl2_type_structure.cpp` | `type.structure` composite type |
| `test_sddl2_type_sizeof.cpp` | Type size calculations |
| `test_sddl2_vm_kind_size.cpp` | Type kind → byte size mapping |
| `test_sddl2_field_extraction.cpp` | Field extraction from segments |
| `test_sddl2_structure_segment.cpp` | Structure-typed segments |
| `test_sddl2_structure_split_integration.cpp` | Structure splitting integration |
| `test_sddl2_load.cpp` | Load operations |
