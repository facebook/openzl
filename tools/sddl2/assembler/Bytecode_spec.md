# OpenZL VM Bytecode Format Specification v0.2

This document describes the **bytecode format** for the OpenZL SDDL2 VM. For the complete instruction set reference (family IDs, opcode values, stack effects), see `src/openzl/compress/graphs/sddl2/sddl2_opcodes.def`, which is the single source of truth.

---

## **1. Basic Encoding**

All instructions are encoded as **32-bit words** in **little-endian** format.

```
Instruction Word (32 bits):
┌──────────────────┬──────────────────┐
│  Family (16-bit) │  Opcode (16-bit) │
└──────────────────┴──────────────────┘
    Low bytes          High bytes

Wire format (4 bytes, little-endian):
[Family_Lo, Family_Hi, Opcode_Lo, Opcode_Hi]
```

**Bytecode Requirements:**
- Total bytecode size must be a multiple of 4 bytes
- Instructions may be followed by immediate operands (see section 3)
- Invalid instruction encodings cause `SDDL2_INVALID_BYTECODE` error

---

## **2. Instruction Set Reference**

**Source of Truth:** `src/openzl/compress/graphs/sddl2/sddl2_opcodes.def`

The VM instruction set is organized into families:

| Family | Purpose | Examples |
|--------|---------|----------|
| **PUSH** | Push constants and values onto stack | `push.zero`, `push.u32`, `push.i64`, `push.tag`, `push.remaining` |
| **MATH** | Arithmetic operations on I64 values | `math.add`, `math.sub`, `math.mul`, `math.div`, `math.mod` |
| **CMP** | Comparison operations | `cmp.eq`, `cmp.ne`, `cmp.lt`, `cmp.le`, `cmp.gt`, `cmp.ge` |
| **LOGIC** | Bitwise logical operations | `logic.and`, `logic.or`, `logic.xor`, `logic.not` |
| **CONTROL** | Control flow | `halt`, `expect_true`, `trace.start` |
| **LOAD** | Load values from input buffer | `load.u8`, `load.i8`, `load.u16le`, `load.i32be`, etc. |
| **STACK** | Stack manipulation | `stack.dup`, `stack.drop`, `stack.swap`, `stack.rot` |
| **TYPE** | Type system operations | `type.fixed_array`, `type.structure`, `type.sizeof` |
| **SEGMENT** | Segment creation | `segment.create_unspecified`, `segment.create_tagged` |

**Reserved families** (not yet implemented):
- **VAR** - Variables (reserved for future use)
- **CALL** - Function calls (reserved for future use)

For complete details (family IDs, opcode values, parameter types, stack effects, descriptions), consult `sddl2_opcodes.def`.

---

## **3. Immediate Value Encoding**

Some instructions require immediate operands that follow the instruction word. All immediate values are encoded in **little-endian** format.

### **3.1 32-bit Immediates (u32, i32)**
- **Size**: 4 bytes
- **Format**: Little-endian
- **Position**: Immediately after instruction word
- **Used by**: `push.u32`, `push.i32`, `push.tag`, and others

### **3.2 64-bit Immediates (i64)**
- **Size**: 8 bytes
- **Format**: Little-endian
- **Position**: Immediately after instruction word
- **Used by**: `push.i64`

---

## **4. Example Programs**

The following examples demonstrate bytecode encoding. **Note**: Opcode values shown are current as of v0.2 but may change in future versions. Always verify against `sddl2_opcodes.def` when generating bytecode.

### **Example 1: Push unsigned 42**
```
Assembly:
  push.u32 42
  halt

Bytecode (hex):
  01 00 02 00   # push.u32 opcode
  2a 00 00 00   # 42 in little-endian u32
  05 00 01 00   # halt

Length: 12 bytes
Final stack: [I64(42)]
```

### **Example 2: Push signed -1**
```
Assembly:
  push.i32 -1
  halt

Bytecode (hex):
  01 00 03 00   # push.i32 opcode
  ff ff ff ff   # -1 in little-endian i32 (two's complement)
  05 00 01 00   # halt

Length: 12 bytes
Final stack: [I64(-1)]
```

### **Example 3: Push large 64-bit value**
```
Assembly:
  push.i64 9223372036854775807
  halt

Bytecode (hex):
  01 00 04 00   # push.i64 opcode
  ff ff ff ff   # 9223372036854775807 in
  ff ff ff 7f   # little-endian i64 (max positive)
  05 00 01 00   # halt

Length: 16 bytes
Final stack: [I64(9223372036854775807)]
```

### **Example 4: Multiple pushes**
```
Assembly:
  push.u32 100
  push.i32 -50
  push.i64 1000000
  halt

Bytecode (hex):
  01 00 02 00   # push.u32 opcode
  64 00 00 00   # 100
  01 00 03 00   # push.i32 opcode
  ce ff ff ff   # -50
  01 00 04 00   # push.i64 opcode
  40 42 0f 00   # 1000000
  00 00 00 00   #
  05 00 01 00   # halt

Length: 28 bytes
Final stack: [I64(100), I64(-50), I64(1000000)]
```

---

## **5. Assembly Language Syntax**

The OpenZL SDDL2 assembler supports the following numeric literal formats:

### **5.1 Decimal (default)**
No prefix. Default format.
```
push.u32 42
push.i32 -100
push.i64 1000000
```

### **5.2 Hexadecimal**
Prefix: `0x` or `0X` (case-insensitive).
```
push.u32 0x2A      # 42
push.i32 0xFF      # 255
push.i64 0xDEADBEEF
```

### **5.3 Binary**
Prefix: `0b` or `0B` (case-insensitive).
```
push.u32 0b101010  # 42
push.i32 0b11111111  # 255
```

**Note**: These three formats are the only formats defined by the OpenZL specification. Any other prefix or format is undefined behavior and may be accepted or rejected by different assembler implementations.

---

## **6. Value Range Validation**

The assembler must validate that immediate values fit within their instruction's range:

- **push.u32**: Must be in range [0, 4294967295]
- **push.i32**: Must be in range [-2147483648, 2147483647]
- **push.i64**: Must be in range [-9223372036854775808, 9223372036854775807]

Out-of-range values should produce an assembly error.
