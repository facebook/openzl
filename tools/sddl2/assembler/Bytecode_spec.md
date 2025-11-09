# OpenZL VM Bytecode Format Specification v0.2

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

## **2. Family Identifiers**

```
0x0001 - PUSH family
0x0002 - MATH family
0x0003 - CMP family
0x0004 - LOGIC family
0x0005 - CONTROL family
0x0006 - LOAD family
0x0007 - STACK family
0x0008 - TYPE family
0x0009 - VAR family
0x000A - EXPECT family
0x000B - CALL family
```

## **3. Opcodes**

### **3.1 CONTROL Family (0x0005)**

#### **3.1.1 control.halt**
- **Opcode**: 0x0001
- **Encoding**: `05 00 01 00` (4 bytes)
- **Stack Effect**: `... → (terminate)`
- **Parameters**: None
- **Description**: Normal program termination

### **3.2 PUSH Family (0x0001)**

#### **3.2.1 push.zero**
- **Opcode**: 0x0001
- **Encoding**: `01 00 01 00` (4 bytes)
- **Stack Effect**: `... → ... I64(0)`
- **Parameters**: None
- **Description**: Push constant zero onto stack

#### **3.2.2 push.u32**
- **Opcode**: 0x0002
- **Encoding**: `01 00 02 00 [value: 4 bytes LE]` (8 bytes total)
- **Stack Effect**: `... → ... I64(value)`
- **Parameters**: 32-bit unsigned immediate (little-endian)
- **Description**: Push unsigned 32-bit value, zero-extended to I64
- **Range**: 0 to 4,294,967,295

#### **3.2.3 push.i32**
- **Opcode**: 0x0003
- **Encoding**: `01 00 03 00 [value: 4 bytes LE]` (8 bytes total)
- **Stack Effect**: `... → ... I64(value)`
- **Parameters**: 32-bit signed immediate (little-endian)
- **Description**: Push signed 32-bit value, sign-extended to I64
- **Range**: -2,147,483,648 to 2,147,483,647

#### **3.2.4 push.i64**
- **Opcode**: 0x0004
- **Encoding**: `01 00 04 00 [value: 8 bytes LE]` (12 bytes total)
- **Stack Effect**: `... → ... I64(value)`
- **Parameters**: 64-bit signed immediate (little-endian)
- **Description**: Push signed 64-bit value
- **Range**: -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807

## **4. Immediate Value Encoding**

Immediate values follow the instruction word and are encoded in little-endian format.

### **4.1 32-bit Immediates (u32, i32)**
- Size: 4 bytes
- Format: Little-endian
- Follows immediately after instruction word

### **4.2 64-bit Immediates (i64)**
- Size: 8 bytes
- Format: Little-endian
- Follows immediately after instruction word

## **5. Example Programs**

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

## **6. Numeric Literal Formats (Language Specification)**

The OpenZL assembly language supports the following numeric literal formats:

### **6.1 Decimal (default)**
No prefix. Default format.
```
push.u32 42
push.i32 -100
push.i64 1000000
```

### **6.2 Hexadecimal**
Prefix: `0x` or `0X` (case-insensitive).
```
push.u32 0x2A      # 42
push.i32 0xFF      # 255
push.i64 0xDEADBEEF
```

### **6.3 Binary**
Prefix: `0b` or `0B` (case-insensitive).
```
push.u32 0b101010  # 42
push.i32 0b11111111  # 255
```

**Note**: These three formats are the only formats defined by the OpenZL specification. Any other prefix or format is undefined behavior and may be accepted or rejected by different assembler implementations.

## **7. Value Range Validation**

The assembler must validate that immediate values fit within their instruction's range:

- **push.u32**: Must be in range [0, 4294967295]
- **push.i32**: Must be in range [-2147483648, 2147483647]
- **push.i64**: Must be in range [-9223372036854775808, 9223372036854775807]

Out-of-range values should produce an assembly error.
