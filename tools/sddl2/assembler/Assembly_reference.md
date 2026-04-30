# SDDL2 Assembly Language Reference

This document consolidates the SDDL2 assembly model, instruction semantics,
and authoring rules in one place. It targets developers writing SDDL2 assembly
directly (without an SDDL front-end compiler).

Source of truth for opcodes and mnemonics: `src/openzl/compress/graphs/sddl2/sddl2_opcodes.def`.

Related documents:
- Assembler usage: `tools/sddl2/assembler/README.md`
- Bytecode format: `tools/sddl2/assembler/Bytecode_spec.md`
- VM details and debugging: `src/openzl/compress/graphs/sddl2/README.md`
- Compiler integration patterns: `src/openzl/compress/graphs/sddl2/COMPILER_INTEGRATION.md`

---

## 1. Execution Model (What the VM Actually Does)

### 1.1 Stack machine

SDDL2 is a linear, stack-based virtual machine. Every instruction consumes
values from a stack and/or pushes values onto the stack.

Stack value kinds:
- **I64**: signed 64-bit integers (all arithmetic, comparisons, sizes)
- **Tag**: 32-bit unsigned segment tag identifiers
- **Type**: a type descriptor (primitive or structure)

There is no control flow (no jumps/branches), no variables, and no calls.
Execution is single-pass, in program order, until `halt`.

### 1.2 Input cursor

The VM tracks an input cursor over the input buffer:
- `push.current_pos` gives the current byte offset (I64).
- `push.remaining` gives bytes left (I64).

**Only** `segment.create_*` instructions advance the cursor. `load.*`
instructions do not advance it; they read at an absolute address.

### 1.3 Segments (Output)

The VM output is a list of segments. Each segment describes a byte range in
the input buffer and an associated type:
- `tag` (u32)
- `start_pos` (byte offset)
- `size_bytes`
- `type` (primitive or structure)

Rules:
- Segments are created **sequentially** in input order; the cursor moves
  forward as segments are created.
- **Consecutive segments with the same tag and type are merged** automatically.
- Tag `0` is reserved for "unspecified" segments (use `segment.create_unspecified`).
- **Tag reuse**: a non-zero tag must always be used with the same type.
  Reusing a tag with a different type is an error.

---

## 2. Assembly Syntax

- **Comments**: `#` or `;` to end of line.
- **Case-sensitive** instruction names.
- **Whitespace** is ignored; one instruction per line is recommended.
- **Numbers**:
  - Decimal: `42`
  - Hex: `0x2A`
  - Binary: `0b101010`
  - Negative values allowed for `i32`/`i64`.

Example:
```asm
# Basic arithmetic
push.i32 10
push.i32 20
math.add
halt
```

---

## 3. Bytecode Encoding (Summary)

Every instruction is a 32-bit little-endian word:
```
[Family_Lo, Family_Hi, Opcode_Lo, Opcode_Hi]
```

Some instructions have immediate operands that follow the instruction word:
- `u32` and `i32`: 4 bytes, little-endian
- `i64`: 8 bytes, little-endian

The VM rejects bytecode whose total size is not a multiple of 4.

---

## 4. Instruction Reference

Notation:
- Stack effects use `a b -> result` (b is top-of-stack).
- `I64`, `Tag`, `Type` refer to stack value kinds.
- Errors include `SDDL2_STACK_UNDERFLOW`, `SDDL2_STACK_OVERFLOW`,
  `SDDL2_TYPE_MISMATCH`, `SDDL2_INVALID_BYTECODE`, plus family-specific errors.

### 4.1 PUSH family (0x0001)

Push constants, immediates, or VM state.

Immediate pushes:
- `push.u32 <u32>`: push I64
- `push.i32 <i32>`: push I64
- `push.i64 <i64>`: push I64
- `push.tag <u32>`: push Tag

Constants/state:
- `push.zero` -> I64(0)
- `push.current_pos` -> current cursor position as I64
- `push.remaining` -> bytes remaining as I64
- `push.stack_depth` -> current stack depth as I64

Type pushes (Type{kind, width=1}):
- `push.type.bytes`
- Integer types: `push.type.u8`, `push.type.i8`, `push.type.u16le`,
  `push.type.u16be`, `push.type.i16le`, `push.type.i16be`,
  `push.type.u32le`, `push.type.u32be`, `push.type.i32le`, `push.type.i32be`,
  `push.type.u64le`, `push.type.u64be`, `push.type.i64le`, `push.type.i64be`
- Floating types: `push.type.f8`, `push.type.f16le`, `push.type.f16be`,
  `push.type.bf16le`, `push.type.bf16be`, `push.type.f32le`, `push.type.f32be`,
  `push.type.f64le`, `push.type.f64be`

Errors:
- `SDDL2_STACK_OVERFLOW` if stack is full.
- `SDDL2_INVALID_BYTECODE` if immediates are missing.
- The assembler enforces immediate range checks.

### 4.2 MATH family (0x0002)

All math operations use signed I64 values.

Binary ops (stack: a b -> a OP b):
- `math.add`
- `math.sub`
- `math.mul`
- `math.div`
- `math.mod`

Unary ops (stack: a -> OP a):
- `math.abs`
- `math.neg`

Errors:
- `SDDL2_STACK_UNDERFLOW`, `SDDL2_TYPE_MISMATCH`
- `SDDL2_DIV_ZERO` for divide/mod by zero
- `SDDL2_MATH_OVERFLOW` on overflow

### 4.3 CMP family (0x0003)

Signed comparisons on I64 values. Result is I64 0 (false) or 1 (true).

Stack: a b -> (a OP b)
- `cmp.eq`, `cmp.ne`, `cmp.lt`, `cmp.le`, `cmp.gt`, `cmp.ge`

Errors: `SDDL2_STACK_UNDERFLOW`, `SDDL2_TYPE_MISMATCH`

### 4.4 LOGIC family (0x0004)

Logical (boolean) operations on I64 values.
Non-zero is true; zero is false. Results are 0 or 1.

Binary (stack: a b -> bool):
- `logic.and`, `logic.or`, `logic.xor`

Unary (stack: a -> bool):
- `logic.not`

Errors: `SDDL2_STACK_UNDERFLOW`, `SDDL2_TYPE_MISMATCH`

### 4.5 CONTROL family (0x0005)

- `halt`: stop execution.
- `expect_true`: stack: value:I64 -> empty.
  Fails with `SDDL2_VALIDATION_FAILED` if value is 0.
- `trace.start`: start collecting trace entries (used by `expect_true` for
  debugging). No stack effect.

Errors for `expect_true`: `SDDL2_STACK_UNDERFLOW`, `SDDL2_TYPE_MISMATCH`,
`SDDL2_VALIDATION_FAILED`.

### 4.6 LOAD family (0x0006)

Load from input buffer at an absolute address.

Stack: addr:I64 -> value:I64

Loads do **not** move the input cursor. Use `segment.create_*` to consume data.

Load instructions:
- 8-bit: `load.u8`, `load.i8`
- 16-bit LE: `load.u16le`, `load.i16le`
- 16-bit BE: `load.u16be`, `load.i16be`
- 32-bit LE: `load.u32le`, `load.i32le`
- 32-bit BE: `load.u32be`, `load.i32be`
- 64-bit: `load.i64le`, `load.i64be`

Errors: `SDDL2_STACK_UNDERFLOW`, `SDDL2_TYPE_MISMATCH`, `SDDL2_LOAD_BOUNDS`

### 4.7 STACK family (0x0007)

General stack manipulation (type-agnostic unless noted).

- `stack.drop`: value -> empty
- `stack.dup`: value -> value value
- `stack.swap`: a b -> b a
- `stack.drop_if`: value cond:I64 -> (value if cond == 0, empty if cond != 0)

Errors: `SDDL2_STACK_UNDERFLOW`, `SDDL2_TYPE_MISMATCH`, `SDDL2_STACK_OVERFLOW`

Notes:
- `stack.over` and `stack.rot` are defined in the opcode table but are not
  currently implemented by the interpreter (they will yield
  `SDDL2_INVALID_BYTECODE`).

### 4.8 TYPE family (0x0008)

Type construction and size queries.

- `type.fixed_array`:
  - Stack: Type count:I64 -> Type
  - Multiplies Type.width by count (count must be >= 0).
- `type.structure`:
  - Stack: Type0 Type1 ... TypeN-1 count:I64 -> Type(STRUCTURE)
  - Pops member_count then that many Types (order preserved).
  - Structure size is the sum of member sizes (no padding).
- `type.sizeof`:
  - Stack: Type -> size_bytes:I64

Errors:
- `SDDL2_STACK_UNDERFLOW`, `SDDL2_TYPE_MISMATCH`
- `SDDL2_MATH_OVERFLOW` (width or size overflow)
- `SDDL2_ALLOCATION_FAILED` (structure metadata allocation)

### 4.9 SEGMENT family (0x000C)

Create output segments and advance the input cursor.

`segment.create_unspecified`:
- Stack: size_bytes:I64 -> empty
- Creates an untagged BYTES segment (tag=0, type=BYTES).
- Advances cursor by size_bytes.

`segment.create_tagged`:
- Stack: Tag Type element_count:I64 -> empty
- Creates a typed segment of size:
  `element_count * sizeof(Type)`
- Registers tag/type pairing (tag must stay consistent across uses).
- Advances cursor by the computed size.

Errors:
- `SDDL2_STACK_UNDERFLOW`, `SDDL2_TYPE_MISMATCH`
- `SDDL2_SEGMENT_BOUNDS` (segment exceeds remaining input)
- `SDDL2_MATH_OVERFLOW` (size calculation overflow)
- `SDDL2_LIMIT_EXCEEDED`, `SDDL2_ALLOCATION_FAILED`

Notes:
- Tag `0` is reserved for unspecified segments; avoid using tag 0 with
  `segment.create_tagged`.
- Consecutive segments with the same tag/type are merged automatically.

### 4.10 Reserved families

The following opcode families are reserved but not implemented:
- `VAR` (0x0009)
- `CALL` (0x000B)

Bytecode using them fails with `SDDL2_INVALID_BYTECODE`.

---

## 5. Common Patterns

### 5.1 Fixed-size header + remaining payload
```asm
# Header: 16 bytes (untagged)
push.i32 16
segment.create_unspecified

# Payload: remaining as u32le elements, tag 1
push.tag 1
push.type.u32le
push.remaining
push.i32 4
math.div                 # element_count = remaining / 4
segment.create_tagged
halt
```

### 5.2 Typed structure segment
```asm
# struct { u8 id; i16le value; }
push.type.u8
push.type.i16le
push.i32 2
type.structure

push.tag 10
stack.swap               # put Type above Tag
push.i32 3               # 3 structs
segment.create_tagged
halt
```

### 5.3 Runtime validation
```asm
# Require input size is exactly 64 bytes
push.remaining
push.i32 64
cmp.eq
expect_true
halt
```

### 5.4 Peek without consuming
```asm
# Read a u16le from current_pos without advancing
push.current_pos
load.u16le
stack.drop               # discard value, cursor unchanged
halt
```

---

## 6. Debugging and Verification

- **Assembler**: `tools/sddl2/assembler/Assembler.h`
- **Disassembler**: `openzl/compress/graphs/sddl2/sddl2_disasm.h`
- **Trace logs**: use `trace.start` and build with traces
  (`make BUILD_TYPE=TRACES_NOSAN LOG_LEVEL=POS`)
- **Bytecode sanity**: size must be multiple of 4; opcodes must exist

---

## 7. Known Limitations (Current State)

- No jumps, labels, macros, data sections, or control-flow beyond `expect_true`.
- `stack.over` and `stack.rot` are defined but not yet implemented.
- `VAR` and `CALL` families are reserved and unimplemented.

---

## 8. Quick Checklist for Authors

- Track stack depth and value kinds after every instruction.
- Use `push.remaining` to size dynamic segments.
- Only `segment.create_*` advances the input cursor.
- Keep tags consistent: same tag => same type.
- Validate inputs with `cmp.*` + `expect_true` where needed.

---

## 9. Instruction List (Auto-generated)

The full instruction list is generated into
`tools/sddl2/assembler/Assembly_opcodes.md` to avoid churn in this guide.

Run:
```
python3 tools/sddl2/assembler/generate_assembly_opcodes.py
```
to regenerate it from `src/openzl/compress/graphs/sddl2/sddl2_opcodes.def`.
