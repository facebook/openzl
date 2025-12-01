# SDDL2 Virtual Machine

Stack-based bytecode interpreter for parsing and decomposing structured data.

## Source of Truth

**`sddl2_opcodes.def`** is the single source of truth for the instruction set.

Several files are auto-generated from this definition and **must not be manually edited**:

- `sddl2_opcodes.h` - C opcode definitions
- `sddl2_disasm_generated.h` - Disassembler implementation
- `tools/sddl2/assembler/opcodes_generated.py` - Python assembler definitions

Each generated file contains a header clearly marking it as auto-generated.

### Regenerating Files

After modifying `sddl2_opcodes.def`:

```bash
# Regenerate C headers (VM)
python3 src/openzl/compress/graphs/sddl2/generate_c_headers.py

# Regenerate Python code (assembler)
python3 tools/sddl2/assembler/generate_opcodes.py
```

## Debug Traces

The SDDL2 VM uses the OpenZL trace system. Enable traces at build time:

```bash
make BUILD_TYPE=TRACES
# or without ASAN overhead:
make BUILD_TYPE=TRACES_NOSAN
```

For instruction-level traces, add `LOG_LEVEL=POS`:

```bash
make BUILD_TYPE=TRACES_NOSAN LOG_LEVEL=POS
```

Equivalently, using environment variables:

```bash
export BUILD_TYPE=TRACES_NOSAN
export LOG_LEVEL=POS
make
```

### Trace Output Format

Each executed instruction produces one trace line:

```
[SDDL2] @0004: math.add (00020001) | stack depth: 1
```

- `@0004` - Program counter (instruction offset in 32-bit words)
- `math.add` - Instruction mnemonic
- `00020001` - Raw 32-bit instruction word (little-endian hex)
- `stack depth: 1` - Stack depth after execution

### Instruction Encoding

32-bit instruction word format:
```
Bits 15-0  (low):  Opcode within family
Bits 31-16 (high): Family ID
```

Example: `00020001` = Family 0x0002 (MATH), Opcode 0x0001 (add)

See `sddl2_opcodes.def` for complete family/opcode mappings.

## Testing

Run SDDL2-specific tests:

```bash
make sddl2_test
```

These tests are fast and useful during VM development. They're also included in `make test`.

## Troubleshooting

### Common Errors

#### `SDDL2_INVALID_BYTECODE`

**Symptoms:**
- VM fails immediately during bytecode loading
- Error occurs before any instructions execute

**Common causes:**
1. Bytecode size is not a multiple of 4 bytes
2. Invalid instruction encoding (family or opcode doesn't exist)
3. Corrupted bytecode buffer

**How to debug:**
```bash
# Check bytecode size
ls -l your_bytecode.bin
# Size must be divisible by 4

# Inspect bytecode hex dump
hexdump -C your_bytecode.bin | head -20

# Compare with known-good bytecode from assembler
python3 tools/sddl2/assembler/sddl2_assembler.py -c "push.zero halt"
# Should output: 01 00 01 00 05 00 01 00
```

**How to fix:**
- Ensure your bytecode generator emits complete 32-bit words
- Verify all family IDs and opcode IDs against `sddl2_opcodes.def`
- Check for off-by-one errors in bytecode buffer sizing

---

#### `SDDL2_STACK_UNDERFLOW`

**Symptoms:**
- VM fails during execution with stack underflow
- Attempting to pop from empty stack

**Common causes:**
1. Incorrect stack depth tracking during compilation
2. Wrong number of operands for instruction
3. Forgot to push required values before operation

**How to debug:**
```bash
# Enable instruction-level traces to see stack depth progression
make BUILD_TYPE=TRACES_NOSAN LOG_LEVEL=POS

# Look for stack depth going to 0 then attempting pop:
# [SDDL2] @0002: push.u32 (00010002) | stack depth: 1
# [SDDL2] @0004: math.add (00020001) | stack depth: 0  ← ERROR: needs 2 values!
```

**How to fix:**
- Review bytecode generation to ensure correct stack discipline
- Track stack depth during compilation (see COMPILER_INTEGRATION.md)
- Verify instruction stack effects match your expectations

---

#### `SDDL2_STACK_OVERFLOW`

**Symptoms:**
- VM fails when stack exceeds capacity
- Usually happens with very deep computations or loops

**Common causes:**
1. Pushing too many values without consuming them
2. Unbounded recursion (if CALL family implemented)
3. Incorrect stack cleanup

**How to debug:**
- Enable traces to see when stack depth grows unexpectedly
- Check for `push.*` instructions without corresponding pops
- Review stack cleanup after operations

**How to fix:**
- Use `stack.drop` to remove unneeded values
- Restructure computation to use fewer intermediate values
- Default limit is 4,096 values (see `sddl2_vm.h`)

---

#### `SDDL2_TYPE_MISMATCH`

**Symptoms:**
- VM fails when operation expects specific value type
- Example: Math operation on Tag value instead of I64

**Common causes:**
1. Pushing Tag when I64 expected (or vice versa)
2. Pushing Type when value expected
3. Stack order confusion

**How to debug:**
```bash
# Enable traces to see value types on stack
make BUILD_TYPE=TRACES_NOSAN LOG_LEVEL=POS

# Check the sequence before the error:
# [SDDL2] @0000: push.tag (00010005) | stack depth: 1  ← Tag value
# [SDDL2] @0002: push.u32 (00010002) | stack depth: 2  ← I64 value
# [SDDL2] @0004: math.add (00020001) | TYPE_MISMATCH  ← Can't add Tag + I64
```

**How to fix:**
- Verify correct push.* instruction for each value type
- Use `push.u32/i32/i64` for numeric values (→ I64)
- Use `push.tag` for segment tags (→ Tag)
- Use `push.type.*` for type descriptors (→ Type)

---

#### `SDDL2_SEGMENT_BOUNDS`

**Symptoms:**
- VM fails when creating segment
- Segment extends beyond input buffer

**Common causes:**
1. Segment size calculation is wrong
2. Not accounting for already-consumed bytes
3. Input data is shorter than expected

**How to debug:**
```bash
# Add validation before segment creation:
push.remaining       # Check how many bytes left
# ... calculate your segment size ...
# Use cmp.* and expect_true to validate
```

**Example validation:**
```asm
# Before: stack has segment_size
stack.dup           # Duplicate size
push.remaining      # Get remaining bytes
cmp.le              # size <= remaining?
expect_true         # Fail with better error if not
# Now safe to create segment
```

**How to fix:**
- Use `push.remaining` to track available bytes
- Add runtime checks with `expect_true`
- Verify input data matches expected format

---

#### `SDDL2_LOAD_BOUNDS`

**Symptoms:**
- VM fails during `load.*` instruction
- Attempting to load from address beyond input buffer

**Common causes:**
1. Load address calculation is wrong
2. Using absolute position instead of relative offset
3. Input shorter than expected

**How to debug:**
```bash
# Enable traces to see the address being loaded
# [SDDL2] @0010: load.u32le (00060008) | stack depth: 1
# Check: was the address on stack valid?
```

**How to fix:**
- Validate addresses before load: `0 <= addr < input_size`
- Use `push.current_pos` to track position
- Add bounds checks with `expect_true`

---

#### `SDDL2_DIV_ZERO`

**Symptoms:**
- VM fails during `math.div` or `math.mod`
- Divisor is zero

**Common causes:**
1. Dynamic calculation produces zero divisor
2. Input data has zero in unexpected place
3. Logic error in size calculation

**How to debug:**
```bash
# Add validation before division:
stack.dup           # Duplicate divisor
push.zero
cmp.ne              # divisor != 0?
expect_true         # Fail if zero
# Now safe to divide
```

**How to fix:**
- Add runtime checks before division
- Handle zero case explicitly in compilation logic
- Validate input format assumptions

---

#### `SDDL2_VALIDATION_FAILED`

**Symptoms:**
- VM fails at `expect_true` instruction
- Runtime validation assertion failed

**Common causes:**
1. Input data doesn't match expected format
2. Magic number / version check failed
3. Field value out of expected range

**How to debug:**
```bash
# Recent versions include rich trace output:
make BUILD_TYPE=TRACES_NOSAN LOG_LEVEL=POS

# Output shows validation context:
# [SDDL2] expect_true FAILED
# [SDDL2] Trace: push.i32 1234 → cmp.eq → expect_true
# [SDDL2] Expected: true, Got: false
```

**How to fix:**
- Check if input data format matches expectations
- Review validation logic for correctness
- Add trace.start before complex validations for debugging

---

### Debugging Strategies

#### 1. Compare with Known-Good Bytecode

```bash
# Generate reference bytecode using assembler
echo "push.u32 42
halt" > test.asm
python3 tools/sddl2/assembler/sddl2_assembler.py test.asm test.bin

# Compare with your compiler output
hexdump -C test.bin
hexdump -C your_output.bin
```

#### 2. Use the Disassembler

```c
#include "openzl/compress/graphs/sddl2/sddl2_disasm.h"

void dump_bytecode(const uint8_t* bytecode, size_t size) {
    const uint32_t* words = (const uint32_t*)bytecode;
    size_t word_count = size / 4;

    for (size_t i = 0; i < word_count; i++) {
        const char* mnemonic = SDDL2_disasm_instruction(words[i]);
        printf("@%04zu: %-20s (0x%08x)\n", i, mnemonic, words[i]);
    }
}
```

#### 3. Incremental Testing

Test bytecode generation in stages:
1. Start with simple: `push.zero halt`
2. Add arithmetic: `push.u32 5 push.u32 3 math.add halt`
3. Add segments: `push.i32 10 segment.create_unspecified halt`
4. Add types: `push.type.u32le push.i32 1 type.fixed_array halt`

#### 4. Unit Test Each Pattern

```c
// Test: Push and arithmetic
SDDL2_Error test_arithmetic() {
    uint8_t bytecode[] = {
        0x01, 0x00, 0x02, 0x00,  // push.u32
        0x05, 0x00, 0x00, 0x00,  // 5
        0x01, 0x00, 0x02, 0x00,  // push.u32
        0x03, 0x00, 0x00, 0x00,  // 3
        0x02, 0x00, 0x01, 0x00,  // math.add
        0x05, 0x00, 0x01, 0x00   // halt
    };

    uint8_t input[] = {0x00};  // Dummy input
    SDDL2_Segment_list segments;
    SDDL2_Segment_list_init(&segments);

    SDDL2_Error err = SDDL2_execute_bytecode(
        bytecode, sizeof(bytecode),
        input, sizeof(input),
        &segments
    );

    SDDL2_Segment_list_destroy(&segments);
    return err;
}
```

---

### Stack Debugging Tips

**Problem: Stack depth doesn't match expectations**

1. **Track stack manually:**
   ```
   Initial:           []
   push.u32 42:       [I64(42)]
   push.u32 10:       [I64(42), I64(10)]
   math.add:          [I64(52)]
   ```

2. **Use traces to verify:**
   ```bash
   [SDDL2] @0000: push.u32 (00010002) | stack depth: 1  ✓
   [SDDL2] @0002: push.u32 (00010002) | stack depth: 2  ✓
   [SDDL2] @0004: math.add (00020001) | stack depth: 1  ✓
   ```

3. **Common stack discipline mistakes:**
   - Forgetting `type.structure` pops N types and member count
   - Not accounting for `segment.create_tagged` popping 3 values
   - Stack order confusion (operations pop TOS first)

**Problem: Segments created but empty/wrong**

1. **Check segment creation arguments:**
   ```asm
   # segment.create_tagged expects: Tag, Type, Count (TOS)
   push.tag 1          # Tag
   push.type.u32le     # Type
   push.i32 100        # Count (TOS - popped first!)
   segment.create_tagged
   ```

2. **Verify with traces that all 3 values are present before call**

3. **Common mistakes:**
   - Wrong stack order (Tag/Type/Count reversed)
   - Count is zero (creates zero-size segment)
   - Type is incorrect (wrong primitive or structure)

---

## Related Documentation

- **Compiler integration**: `COMPILER_INTEGRATION.md` (this directory) - Complete guide for compiler writers
- **Assembler**: `tools/sddl2/assembler/README.md` - Assembler tool documentation
- **Bytecode format**: `tools/sddl2/assembler/Bytecode_spec.md` - Format specification
- **Test framework**: `tests/compress/graphs/sddl2/BYTECODE_TEST_FRAMEWORK.md` - Auto-discovery testing
- **Examples**: `examples/sddl2_asm/` - Working programs (start with `sao_silesia.asm`)
