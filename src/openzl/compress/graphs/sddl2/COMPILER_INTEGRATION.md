# SDDL2 VM Compiler Integration Guide

This guide is for developers implementing compilers that target the SDDL2 VM bytecode format.

---

## Quick Start

### 1. Essential Reading (in order)

1. **Bytecode format**: `tools/sddl/assembler/Bytecode_spec.md`
   - Instruction encoding (32-bit words, little-endian)
   - Immediate value encoding
   - Example programs with hex dumps

2. **Instruction set**: `sddl2_opcodes.def` (this directory)
   - **Source of truth** for all opcodes
   - Family IDs, opcode values, parameter types, descriptions

3. **Working examples** (study in order):
   - `examples/sddl2_asm/sao_silesia.asm` - Start here (star catalog parser)
   - `examples/sddl2_asm/sao_full.asm` - Next step (extended features)
   - Both demonstrate dynamic arrays, structures, tagged segments

4. **VM API**: `sddl2_interpreter.h` (this directory)
   - Entry point: `SDDL2_execute_bytecode()`
   - Parameters: bytecode, input data, output segments

### 2. Key Concepts

**Stack-Based Execution**
- VM operates on a stack of typed values (I64, Tag, Type)
- All operations manipulate the stack
- Track stack depth during compilation to avoid underflow/overflow

**Segments**
- Output: list of tagged byte ranges over input buffer
- Each segment has: tag (u32), start position, size, type
- VM validates segments don't exceed input bounds

**Types**
- 24 primitive types (U8, I16LE, F32BE, BF16LE, etc.)
- Composite types: structures (via `type.structure`)
- Arrays: encoded as type with width > 1

---

## Code Generation Walkthrough

### Example: Generate "Push 42 and halt"

**Assembly:**
```asm
push.u32 42
halt
```

**Bytecode generation (pseudo-code):**
```python
def emit_instruction(family_id, opcode_id):
    # 32-bit word: [family:16][opcode:16] in little-endian
    word = (opcode_id << 16) | family_id
    emit_u32_le(word)

def emit_u32_le(value):
    bytecode.append(value & 0xFF)
    bytecode.append((value >> 8) & 0xFF)
    bytecode.append((value >> 16) & 0xFF)
    bytecode.append((value >> 24) & 0xFF)

# Generate: push.u32 42
emit_instruction(0x0001, 0x0002)  # PUSH family, push.u32 opcode
emit_u32_le(42)                    # Immediate operand

# Generate: halt
emit_instruction(0x0005, 0x0001)  # CONTROL family, halt opcode

# Result: [01 00 02 00 2a 00 00 00 05 00 01 00]
```

### Example: Create a tagged segment

**Task:** Parse a 28-byte header, then create a segment tagged "header" (tag=1)

**Assembly:**
```asm
push.i32 28              # Size
push.tag 1               # Tag
segment.create_tagged    # Create segment: pops tag, size
halt
```

**Bytecode generation:**
```python
emit_instruction(0x0001, 0x0003)  # push.i32
emit_u32_le(28)
emit_instruction(0x0001, 0x0005)  # push.tag
emit_u32_le(1)
emit_instruction(0x000C, 0x0002)  # segment.create_tagged
emit_instruction(0x0005, 0x0001)  # halt
```

**Stack discipline:**
```
Initial:      []
push.i32 28:  [I64(28)]
push.tag 1:   [I64(28), Tag(1)]
segment.create_tagged: []  (pops 2, creates segment, pushes nothing)
halt:         []
```

---

## Common Patterns

### 1. Fixed-Size Header
```asm
push.i32 <header_size>
segment.create_unspecified   # Or with a tag for typed header
```

### 2. Dynamic Array Count
```asm
push.remaining        # Bytes left in input
push.i32 <item_size>  # Bytes per item
math.div              # Count = remaining / item_size
```

### 3. Building Structure Types
```asm
# Example: struct { u32 id; f32 value; }
push.type.u32le       # Member 1
push.type.f32le       # Member 2
push.i32 2            # Member count
type.structure        # Create structure type
```

### 4. Tagged Segments with Types
```asm
push.tag <my_tag>     # Segment tag
push.type.f64le       # Element type
push.i32 <count>      # Element count
segment.create_tagged # Creates typed segment
```

### 5. Validation
```asm
# Assert that a value equals expected
push.i32 <expected>
cmp.eq                # Compare with value on stack
expect_true           # Fail if not equal
```

---

## Stack Discipline

**Critical:** Track stack depth during compilation to prevent:
- **Stack underflow**: Popping from empty stack → `SDDL2_STACK_UNDERFLOW`
- **Stack overflow**: Exceeding capacity → `SDDL2_STACK_OVERFLOW`

**Stack effects** (examples from `sddl2_opcodes.def`):
```
push.u32 <val>:         ... → ... I64(val)           (+1)
math.add:               ... I64(a) I64(b) → ... I64(a+b)  (-1)
segment.create_tagged:  ... Tag Type I64 → ...       (-3)
stack.dup:              ... V → ... V V               (+1)
stack.swap:             ... A B → ... B A             (±0)
```

**Validation checklist:**
- Start with empty stack
- After each instruction, verify stack depth ≥ 0
- Program end: stack can be non-empty (but usually should be empty)

---

## Error Handling

### Error Codes (from `sddl2_error.h`)

| Error | Cause | Compiler Fix |
|-------|-------|--------------|
| `SDDL2_STACK_UNDERFLOW` | Popped from empty stack | Check stack depth before emitting pop instructions |
| `SDDL2_STACK_OVERFLOW` | Stack capacity exceeded | Reduce stack usage or increase limit |
| `SDDL2_TYPE_MISMATCH` | Wrong value type for operation | Track value types on stack |
| `SDDL2_LOAD_BOUNDS` | Load address out of bounds | Validate load addresses are within input |
| `SDDL2_SEGMENT_BOUNDS` | Segment extends beyond input | Validate segment sizes don't exceed remaining input |
| `SDDL2_DIV_ZERO` | Division by zero | Add runtime check or guarantee non-zero divisor |
| `SDDL2_INVALID_BYTECODE` | Malformed bytecode | Ensure bytecode size is multiple of 4 |
| `SDDL2_VALIDATION_FAILED` | `expect_true` failed | Check validation logic |

---

## Debugging Your Compiler Output

### 1. Enable VM Traces

Build with trace support to see instruction-by-instruction execution:

```bash
make BUILD_TYPE=TRACES_NOSAN LOG_LEVEL=POS
```

Example trace output:
```
[SDDL2] @0000: push.u32 (00010002) | stack depth: 1
[SDDL2] @0002: push.remaining (00010081) | stack depth: 2
[SDDL2] @0003: math.div (00020004) | stack depth: 1
[SDDL2] @0004: segment.create_tagged (000C0002) | stack depth: 0
[SDDL2] @0005: halt (00050001) | stack depth: 0
```

Trace format:
- `@0000` - Program counter (word offset, not byte offset)
- `push.u32` - Instruction mnemonic
- `(00010002)` - Raw instruction encoding (hex)
- `stack depth: 1` - Stack depth **after** instruction executes

### 2. Use the Disassembler

The VM includes a disassembler based on `sddl2_opcodes.def`:

```c
#include "openzl/compress/graphs/sddl2/sddl2_disasm.h"

void debug_bytecode(const uint8_t* bytecode, size_t size) {
    for (size_t i = 0; i < size; i += 4) {
        uint32_t instr = *(uint32_t*)(bytecode + i);
        const char* mnemonic = SDDL2_disasm_instruction(instr);
        printf("@%04zx: %s\n", i/4, mnemonic);
    }
}
```

### 3. Validate Bytecode Structure

**Before submitting to VM:**
- Check: `bytecode_size % 4 == 0`
- Verify: All instruction words are valid (family exists, opcode exists)
- Scan for immediates: Instructions with parameters have correct immediate sizes

---

## Type System Deep Dive

### Primitive Types

24 built-in types (from `sddl2_vm.h`):

| Type | Size | Description |
|------|------|-------------|
| `BYTES` | 1 | Raw byte |
| `U8` / `I8` | 1 | Unsigned/signed 8-bit |
| `U16LE` / `U16BE` | 2 | Unsigned 16-bit (little/big endian) |
| `I16LE` / `I16BE` | 2 | Signed 16-bit |
| `U32LE` / `U32BE` | 4 | Unsigned 32-bit |
| `I32LE` / `I32BE` | 4 | Signed 32-bit |
| `U64LE` / `U64BE` | 8 | Unsigned 64-bit |
| `I64LE` / `I64BE` | 8 | Signed 64-bit |
| `F16LE` / `F16BE` | 2 | IEEE 754 half-precision float |
| `BF16LE` / `BF16BE` | 2 | Brain float 16 |
| `F32LE` / `F32BE` | 4 | IEEE 754 single-precision float |
| `F64LE` / `F64BE` | 8 | IEEE 754 double-precision float |

**Push primitive types:**
```asm
push.type.u32le    # Pushes Type{U32LE, width=1}
push.type.f64be    # Pushes Type{F64BE, width=1}
```

### Composite Types

**Fixed-size arrays:**
```asm
# Create type: F32LE[100]
push.type.f32le     # Base type
push.i32 100        # Array size
type.fixed_array    # Pops: type, size → Pushes: Type{F32LE, width=100}
```

**Structures:**
```asm
# struct Point { f32 x; f32 y; f32 z; }
push.type.f32le     # Member 1: x
push.type.f32le     # Member 2: y
push.type.f32le     # Member 3: z
push.i32 3          # Member count
type.structure      # Creates: Type{STRUCTURE, width=1, complex_data=...}
```

**Nested structures:**
```asm
# struct Data { u32 id; Point pos; }
push.type.u32le     # Member 1: id
push.type.f32le     # Point.x
push.type.f32le     # Point.y
push.type.f32le     # Point.z
push.i32 3
type.structure      # Creates Point type
push.i32 2          # Data has 2 members: u32, Point
type.structure      # Creates Data type
```

### Type Sizing

Use `type.sizeof` to get byte size of a type:

```asm
push.type.f64le
type.sizeof         # Pushes I64(8)

# For structures:
push.type.u32le
push.type.f32le
push.i32 2
type.structure      # Creates struct { u32; f32; }
type.sizeof         # Pushes I64(8)  (4 + 4)
```

---

## Performance Considerations

### Stack Depth Limits

- **Default limit**: 4,096 values
- **Hard limit**: 512,384 values (compile-time constant)
- Compilers should aim for shallow stack usage

### Segment Limits

- **Maximum segments**: 512,384 (hard limit)
- Plan segment granularity accordingly

### Memory Allocation

- **Production mode**: Uses arena allocation from `ZL_Graph_getScratchSpace()`
- **Test mode**: Uses standard `malloc`/`realloc`
- Compilers don't control allocation directly

---

## Testing Your Compiler

### 1. Unit Test Template

```c
#include "openzl/compress/graphs/sddl2/sddl2_interpreter.h"

void test_my_compiler_output() {
    // Your compiler generates this
    uint8_t bytecode[] = {
        0x01, 0x00, 0x02, 0x00,  // push.u32
        0x2a, 0x00, 0x00, 0x00,  // 42
        0x05, 0x00, 0x01, 0x00   // halt
    };

    // Test input
    uint8_t input[] = {0xAA, 0xBB, 0xCC};

    // Execute
    SDDL2_Segment_list segments;
    SDDL2_Segment_list_init(&segments);

    SDDL2_Error err = SDDL2_execute_bytecode(
        bytecode, sizeof(bytecode),
        input, sizeof(input),
        &segments
    );

    assert(err == SDDL2_OK);
    // Validate segments...

    SDDL2_Segment_list_destroy(&segments);
}
```

### 2. Integration Test Strategy

1. **Compile known-good assembly** using the Python assembler
2. **Compile same logic** using your compiler
3. **Compare bytecode** byte-for-byte (or segment outputs)

### 3. Regression Test Suite

Maintain a corpus of SDDL programs and their expected bytecode:
```
tests/
  hello.sddl → hello.bin + hello.expected_segments
  header.sddl → header.bin + header.expected_segments
  ...
```

---

## API Reference

### Main Entry Point

```c
SDDL2_Error SDDL2_execute_bytecode(
    const void* bytecode,           // Bytecode buffer (must be 4-byte aligned)
    size_t bytecode_size,           // Bytecode size in bytes (must be multiple of 4)
    const void* input_data,         // Input data buffer
    size_t input_size,              // Input data size in bytes
    SDDL2_Segment_list* output_segments  // Output: populated segment list (must be initialized)
);
```

**Returns:** `SDDL2_OK` on success, error code on failure

**Preconditions:**
- `bytecode_size % 4 == 0`
- `output_segments` initialized via `SDDL2_Segment_list_init()`

**Postconditions:**
- On success: `output_segments` contains parsed segments
- On failure: `output_segments` state is undefined (destroy anyway)
- Caller must call `SDDL2_Segment_list_destroy()` when done

---

## Reserved Features

The following instruction families are **reserved** but not yet implemented:

- **VAR (0x0009)**: Variables
- **CALL (0x000B)**: Function calls

Bytecode using these families will fail with `SDDL2_INVALID_BYTECODE`.

**Planning ahead:** If/when these are implemented, bytecode format may remain compatible, but no guarantees until stabilized.

---

## FAQ

**Q: Can I use jumps or branches?**
A: No. The VM is single-pass, no control flow. Use `expect_true` for validation only.

**Q: How do I handle variable-length data?**
A: Use `push.remaining` to get remaining bytes, then calculate count dynamically.

**Q: What's the difference between `segment.create_unspecified` and `segment.create_tagged`?**
A: `create_unspecified` has no tag (used for headers/metadata). `create_tagged` requires tag + type for typed data streams.

**Q: Can I create segments out of order?**
A: No. Segments must be created sequentially as you traverse the input forward.

**Q: What if my input doesn't parse completely?**
A: VM will succeed even if some bytes remain unconsumed. Use `push.remaining` with `expect_true` to enforce complete parsing:
```asm
push.remaining
push.zero
cmp.eq          # remaining == 0?
expect_true     # Fail if not
```

**Q: How do I handle byte alignment/padding?**
A: Create segments for padding bytes, or use `load.*` instructions to skip them without creating segments.

**Q: Where should I report compiler bugs vs VM bugs?**
A: [Add your project's bug tracker info here]

---

## Additional Resources

- **VM README**: `README.md` (this directory) - Debugging, testing, source of truth
- **Bytecode spec**: `tools/sddl/assembler/Bytecode_spec.md` - Format details
- **Assembler README**: `tools/sddl/assembler/README.md` - Assembler tool documentation
- **Test framework**: `tests/compress/graphs/sddl2/BYTECODE_TEST_FRAMEWORK.md` - Auto-discovery testing
- **Example programs**: `examples/sddl2_asm/` - Study `sao_silesia.asm` first, then `sao_full.asm`

---

**Last updated:** 2025-11-19
**Bytecode format version:** v0.2
