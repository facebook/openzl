# SDDL2 Assembler

A Python assembler that converts SDDL2 assembly language into bytecode for the OpenZL SDDL2 Virtual Machine.

## Quick Start

**Assemble a file:**
```bash
python3 sddl2_assembler.py program.asm output.bin
```

**Assemble from command line (displays hex output):**
```bash
python3 sddl2_assembler.py -c "push.i32 42 halt"
# 03 00 01 00 2a 00 00 00 01 00 05 00
```

## Overview

The SDDL2 assembler bridges human-readable assembly and VM-executable bytecode. It's used for:

- **Testing**: Writing test cases for the SDDL2 VM (see `../../../tests/compress/graphs/sddl2/asm/`)
- **Development**: Rapid prototyping of bytecode programs
- **Debugging**: Hand-crafted programs for VM feature validation
- **Examples**: Demonstrating VM capabilities (see `../../../examples/sddl2_asm/`)

> **For compiler writers**: If you're generating bytecode programmatically, see `../../../src/openzl/compress/graphs/sddl2/COMPILER_INTEGRATION.md` for the integration API documentation.
>
> **For assembly authors**: See `Assembly_reference.md` for the consolidated execution model, instruction semantics, and authoring rules.

## Assembly Syntax

### Basic Structure

```asm
# Comments start with # or ;
# One instruction per line (or whitespace-separated)

push.i32 10        # Push integer 10
push.i32 20        # Push integer 20
math.add           # Add them: 10 + 20 = 30
halt               # Stop execution
```

### Syntax Rules

- **One instruction per line** (or separated by whitespace)
- **Comments**: Start with `#` or `;`, continue to end of line
- **Case-sensitive**: Instruction names must match exactly
- **Whitespace**: Extra whitespace ignored
- **Numbers**: Decimal (42), hexadecimal (0x2A), negative (-10)

## Instruction Set

The assembler supports a large SDDL2 instruction set, which is **auto-generated** from the VM opcode definitions.

> **Complete instruction reference**: See `opcodes_generated.py` for the definitive, always-up-to-date list of all instructions.

### How Instructions Are Defined

1. **Source of truth**: `../../../src/openzl/compress/graphs/sddl2/sddl2_opcodes.def` defines all opcodes
2. **Auto-generation**: Run `python3 generate_opcodes.py` to create `opcodes_generated.py`
3. **Synchronization**: Keeps assembler in sync with VM implementation

### Viewing All Available Instructions

```python
# List all instructions
python3 -c "from opcodes_generated import INSTRUCTIONS; print('\n'.join(sorted(INSTRUCTIONS.keys())))"

# Show instruction families
python3 -c "from opcodes_generated import FAMILIES; print(FAMILIES)"
```

### Key Instruction Families

At the time of this wrting, the instruction set is organized into these families (see `opcodes_generated.py` for complete details):

**CONTROL (0x0005)** - Control flow
- `halt`, `expect_true`, `trace.start`

**PUSH (0x0001)** - Push values and types
- Constants: `push.zero`, `push.u32`, `push.i32`, `push.i64`, `push.tag`
- Introspection: `push.current_pos`, `push.remaining`, `push.stack_depth`
- Types: `push.type.u8`, `push.type.i32le`, `push.type.f64be`, etc. (24+ type descriptors)

**STACK (0x0007)** - Stack manipulation
- `stack.dup`, `stack.over`, `stack.drop`, `stack.swap`, `stack.rot`, `stack.drop_if`

**MATH (0x0002)** - Arithmetic operations
- `math.add`, `math.sub`, `math.mul`, `math.div`, `math.mod`, `math.abs`, `math.neg`

**CMP (0x0003)** - Comparison operations
- `cmp.eq`, `cmp.ne`, `cmp.lt`, `cmp.le`, `cmp.gt`, `cmp.ge`

**LOGIC (0x0004)** - Bitwise/logical operations
- `logic.and`, `logic.or`, `logic.xor`, `logic.not`

**LOAD (0x0006)** - Load from input buffer
- 8-bit: `load.u8`, `load.i8`
- 16-bit: `load.u16le`, `load.i16le`, `load.u16be`, `load.i16be`
- 32-bit: `load.u32le`, `load.i32le`, `load.u32be`, `load.i32be`
- 64-bit: `load.i64le`, `load.i64be`

**TYPE (0x0008)** - Type construction
- `type.fixed_array`, `type.structure`, `type.sizeof`

**SEGMENT (0x000C)** - Segment creation
- `segment.create_unspecified`, `segment.create_tagged`

> **Note**: The VAR (0x0009) and CALL (0x000B) families are reserved but not yet implemented.

## Examples

### Example 1: Simple Arithmetic
```asm
# Calculate (5 + 3) * 2
push.i32 5
push.i32 3
math.add          # Stack: [8]
push.i32 2
math.mul          # Stack: [16]
halt
```

### Example 2: Dynamic Segment Size
```asm
# Create segment with all remaining input
push.remaining    # Push remaining bytes
segment.create_unspecified
halt
```

### Example 3: Typed Segment
```asm
# Create a segment of 10 U32LE values
push.tag 100           # Segment tag
push.type.u32le        # Element type
push.i32 10            # Element count
segment.create_tagged  # Creates 40-byte segment
halt
```

### Example 4: Array Type
```asm
# Create type for U32LE[10] (array of 10 U32LE values)
push.type.u32le        # Base type
push.i32 10            # Array length
type.fixed_array       # Creates Type{U32LE, width=10}

# Use it to create a segment with 5 arrays
push.tag 200
stack.swap             # Put type on top
push.i32 5             # 5 arrays
segment.create_tagged  # Creates 200-byte segment (5 × 10 × 4)
halt
```

### Example 5: Structure Type
```asm
# Define struct { u8 id; i16le value; }
push.type.u8           # Member 1
push.type.i16le        # Member 2
push.i32 2             # Member count
type.structure         # Creates structure type

# Create segment with 10 structures
push.tag 300
stack.swap
push.i32 10
segment.create_tagged  # Creates 30-byte segment (10 × 3)
halt
```

### Example 6: Validation
```asm
# Assert input size is exactly 100 bytes
push.remaining
push.i32 100
cmp.eq
expect_true            # Error if not equal
halt
```

### Example 7: Real-World Parser (SAO Format)
```asm
# Parse SAO star catalog header
push.i32 28
segment.create_unspecified

# Build StarEntry structure type: {Float64LE, Float64LE, Byte[2], UInt16LE, Float32LE, Float32LE}
push.type.f64le
push.type.f64le
push.type.u8
push.i32 2
type.fixed_array       # Byte[2]
push.type.u16le
push.type.f32le
push.type.f32le
push.i32 6
type.structure

# Calculate number of star records
push.remaining
push.i32 28            # Size of StarEntry
math.div               # star_count = remaining / 28

# Create segment with all stars
push.tag 1
stack.rot              # Move type to TOS
segment.create_tagged
halt
```

## Bytecode Format

All instructions are encoded as 32-bit little-endian words:

```
Bytes:    [Family_Lo, Family_Hi, Opcode_Lo, Opcode_Hi]
Encoding: (opcode << 16) | family
```

**Instructions with immediates:**
- `push.u32 42`: `[01 00 02 00] [2a 00 00 00]` (instruction + u32 immediate)
- `push.i64 -1`: `[01 00 04 00] [ff ff ff ff ff ff ff ff]` (instruction + i64 immediate)

**Instructions without immediates:**
- `halt`: `[05 00 01 00]` (CONTROL family 0x0005, opcode 0x0001)
- `math.add`: `[02 00 01 00]` (MATH family 0x0002, opcode 0x0001)

For complete bytecode specification, see `Bytecode_spec.md`.

## Testing

### Run the assembler test suite:
```bash
cd tests
python3 test_assembler.py
```

### Test structure:
- `tests/success/` - Programs that should assemble successfully
- `tests/fail/` - Programs that should fail with specific errors
- `regenerate_expected.sh` - Update expected outputs after opcode changes

## Opcode Generation

The assembler uses auto-generated opcode definitions from the VM source:

```bash
# Regenerate opcodes from sddl2_opcodes.def
python3 generate_opcodes.py
```

This creates `opcodes_generated.py` with instruction definitions synchronized with the VM.

**Source of truth**: `../../../src/openzl/compress/graphs/sddl2/sddl2_opcodes.def`

## Related Documentation

- **Assembly Reference**: `Assembly_reference.md` - Execution model, instruction semantics, authoring rules
- **Opcode Reference**: `Assembly_opcodes.md` - Auto-generated full instruction list
- **Bytecode Specification**: `Bytecode_spec.md` - Complete bytecode format details
- **VM Documentation**: `../../../src/openzl/compress/graphs/sddl2/README.md` - VM features and debugging
- **Compiler Integration**: `../../../src/openzl/compress/graphs/sddl2/COMPILER_INTEGRATION.md` - For programmatic bytecode generation
- **Test Examples**: `../../../tests/compress/graphs/sddl2/asm/` - 50+ test programs
- **Real-World Examples**: `../../../examples/sddl2_asm/` - Production-ready parsers

## File Structure

```
tools/sddl2/assembler/
├── sddl2_assembler.py       # Main assembler implementation
├── opcodes_generated.py     # Auto-generated opcode definitions
├── generate_opcodes.py      # Opcode generator script
├── Assembly_reference.md    # Execution model + instruction semantics
├── Assembly_opcodes.md      # Auto-generated opcode list
├── Bytecode_spec.md         # Bytecode format specification
├── README.md                # This file
└── tests/                   # Test suite
    ├── test_assembler.py
    ├── regenerate_expected.sh
    ├── success/             # Valid assembly programs
    └── fail/                # Invalid programs (for error testing)
```

## Common Workflows

### Writing a New Test Case
1. Create `.asm` file in `../../../tests/compress/graphs/sddl2/asm/`
2. Add metadata comments:
   ```asm
   # Test: Description of what this tests
   # Expected: SDDL2_OK  (or error code)
   ```
3. Regenerate test bytecode:
   ```bash
   cd ../../../tests/compress/graphs/sddl2
   python3 generate_test_bytecode.py
   ```

### Debugging VM Programs
1. Write assembly with `trace.start` for detailed execution logs
2. Assemble and run with trace-enabled VM build
3. Review instruction-by-instruction execution

### Adding New Instructions
1. Update `../../../src/openzl/compress/graphs/sddl2/sddl2_opcodes.def`
2. Regenerate C headers: `python3 src/openzl/compress/graphs/sddl2/generate_c_headers.py`
3. Regenerate assembler opcodes: `python3 tools/sddl2/assembler/generate_opcodes.py`
4. Update this README with instruction documentation

## Error Handling

The assembler provides clear error messages:

```bash
$ python3 sddl2_assembler.py -c "push.u32"
Assembly error: Instruction 'push.u32' requires 1 parameter(s), but only 0 provided
```

```bash
$ python3 sddl2_assembler.py -c "invalid_instruction"
Assembly error: Unknown instruction or unexpected token: invalid_instruction
```

## Version Information

- **Bytecode Format Version**: See `Bytecode_spec.md`
- **VM Compatibility**: SDDL2 VM (Phase 1-5 complete)
- **Opcode Definitions**: Auto-synced from `sddl2_opcodes.def`

---

**Last Updated**: 2025-12-01
**Instruction Count**: 70+ instructions across 9 families
