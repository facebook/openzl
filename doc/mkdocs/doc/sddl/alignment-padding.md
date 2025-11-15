# Alignment and Padding

*Chapter 6 - Layout constraints*

Binary formats often have specific alignment and padding requirements. Hardware may require certain data types to start at aligned addresses, or formats may enforce fixed record sizes. This chapter explains how SDDL describes these layout constraints.

---

## Field Alignment with `align(n)`

The `align(n)` modifier specifies that a field must start at an address that is a multiple of `n` bytes.

### Basic Syntax

```sddl
Record Data() = {
  flags: UInt8,
  value: align(8) Int64LE  # Must start at 8-byte boundary
}
```

If `flags` ends at byte 1, the `value` field will start at byte 8 (the next 8-byte boundary), leaving 7 bytes of padding between them.

### Common Alignment Values

- `align(4)` - 4-byte alignment (common for 32-bit integers)
- `align(8)` - 8-byte alignment (common for 64-bit integers and doubles)
- `align(16)` - 16-byte alignment (SIMD register size)
- `align(64)` - 64-byte alignment (cache line size on many systems)

### Alignment in Records

```sddl
Record Header() = {
  magic: Bytes(4),
  version: UInt16LE,
  _: Bytes(2),                    # Explicit padding to 8 bytes
  timestamp: align(8) Int64LE,    # Aligned 64-bit value
  checksum: UInt32LE
}
```

The `align(8)` ensures `timestamp` starts at an 8-byte boundary, regardless of what precedes it.

### Alignment in Arrays

When a type has alignment requirements, arrays of that type include inter-element padding:

```sddl
Record Entry() = {
  id: UInt8,
  value: align(8) Float64LE
}

entries: Entry[100]
```

Each `Entry` in the array will have padding after `id` to ensure `value` is 8-byte aligned. This padding is repeated for every element.

### Alignment and Instant-Parse

Alignment arguments must be constant or parameter-based for instant-parse:

```sddl
@instant_parse
Record Data(align_size) = {
  header: Bytes(10),
  payload: align(align_size) Bytes(100)  # OK: depends on parameter
}
```

Using a local field for alignment breaks instant-parse:

```sddl
Record Data() = {
  align_req: UInt8,
  payload: align(align_req) Bytes(100)  # Not instant-parse
}
```

---

## Record Padding

SDDL provides two directives for controlling the total size of a record.

### `pad_to n` - Exact Size

`pad_to n` enforces that a record is exactly `n` bytes. If the record's natural size is less than `n`, padding is added. If it's greater, the compiler reports a format error.

```sddl
Record Header() = {
  magic: Bytes(4),
  version: UInt16LE,
  flags: UInt16LE
} pad_to 16
```

This `Header` is exactly 16 bytes. The natural size is 8 bytes, so 8 bytes of padding are added.

**Format error example:**
```sddl
Record TooBig() = {
  data: Bytes(20)
} pad_to 16  # ERROR: Record is 20 bytes, cannot pad to 16
```

### `pad_align n` - Round to Multiple

`pad_align n` rounds the record size up to the next multiple of `n` bytes.

```sddl
Record Scanline(width) = {
  pixels: UInt8[width]
} pad_align 4
```

If `width` is 10, the natural size is 10 bytes. With `pad_align 4`, the record becomes 12 bytes (the next multiple of 4).

### Combining `pad_to` and `pad_align`

When both are present, `pad_to` is applied first, then `pad_align`:

```sddl
Record Block(size) = {
  data: Bytes(size)
} pad_to 100 pad_align 16
```

This ensures the record is at least 100 bytes, then rounds up to the next multiple of 16. If `size` is 50:

1. `pad_to 100` makes it 100 bytes
2. `pad_align 16` rounds 100 up to 112 bytes (next multiple of 16)

### Parameterized Padding

Padding values can be parameters:

```sddl
Record Container(min_size, align_to) = {
  header: Bytes(16),
  payload: Bytes(100)
} pad_to min_size pad_align align_to
```

The padding depends on parameters passed when the record is instantiated. This maintains instant-parse status.

---

## Practical Examples

### Example 1: Hardware-Aligned Structure

```sddl
# Structure matching hardware expectations
Record DeviceRegister() = {
  control: UInt32LE,
  _: Bytes(4),                         # Explicit padding
  address: align(8) UInt64LE,          # 8-byte aligned pointer
  data: align(16) Bytes(16)            # 16-byte aligned data block
}
```

This describes a structure where hardware requires specific alignment for direct memory access.

### Example 2: Fixed-Size Records

```sddl
# Database records, all exactly 256 bytes
Record DatabaseRecord() = {
  id: UInt64LE,
  name: Bytes(64),
  email: Bytes(128),
  created: Int64LE,
  flags: UInt32LE
} pad_to 256
```

The natural size is 8+64+128+8+4 = 212 bytes. `pad_to 256` adds 44 bytes of padding to make each record exactly 256 bytes.

### Example 3: Cache-Line Aligned Records

```sddl
# Each record aligned to 64-byte cache line
Record CacheOptimized() = {
  hot_data: Bytes(32),    # Frequently accessed data
  cold_data: Bytes(16)    # Less frequently accessed
} pad_align 64
```

The natural size is 48 bytes. `pad_align 64` rounds up to 64 bytes, ensuring each record fits in one cache line.

### Example 4: Format with Variable Header

```sddl
Record File(header_size) = {
  header_data: Bytes(header_size)
} pad_to header_size pad_align 512

size: UInt16LE
file_header: File(size)
```

The header is at least `header_size` bytes, rounded up to a 512-byte boundary (common disk sector size).

### Example 5: Array with Padding

```sddl
Record Entry() = {
  timestamp: Int64LE,
  value: Float32LE
} pad_align 16

entries: Entry[1000]
```

Each entry is 12 bytes naturally (8+4), but `pad_align 16` makes each 16 bytes. The array has consistent 16-byte elements with 4 bytes of padding each.

---

## Understanding Padding Bytes

Padding bytes are "don't care" values. SDDL doesn't specify what they contain:

```sddl
Record Padded() = {
  value: UInt32LE
} pad_to 16
```

The 12 padding bytes after `value` can contain any data. SDDL just skips over them. Some formats zero padding bytes, others leave them uninitialized—SDDL describes the layout, not the content of padding.

---

## Alignment Propagation

Alignment requirements propagate through nested structures:

```sddl
Record Inner() = {
  value: align(8) Float64LE
}

Record Outer() = {
  header: Bytes(4),
  inner: Inner  # inner.value requires 8-byte alignment
}
```

The `Inner` record's alignment requirement affects the `Outer` record's layout. The compiler tracks this automatically.

---

## Common Patterns

### Pattern: Fixed-Size Message

```sddl
# Network protocol with fixed 128-byte messages
Record Message() = {
  type: UInt8,
  length: UInt16LE,
  payload: Bytes(100)
} pad_to 128
```

### Pattern: Sector-Aligned Data

```sddl
# Disk sector alignment (512 bytes)
Record Sector(data_size) = {
  data: Bytes(data_size)
} pad_align 512
```

### Pattern: SIMD-Friendly Layout

```sddl
# 16-byte aligned for SIMD operations
Record Vector4() = {
  x: Float32LE,
  y: Float32LE,
  z: Float32LE,
  w: Float32LE
} pad_align 16
```

### Pattern: Explicit Padding for Clarity

```sddl
# Sometimes explicit is clearer than implicit
Record Clear() = {
  id: UInt32LE,
  _: Bytes(4),              # Explicit 4-byte padding
  timestamp: Int64LE
}
```

This makes padding visible rather than relying on `align()`.

---

## Alignment and Instant-Parse

Alignment and padding interact with instant-parse:

**Instant-Parse (constant/parameter alignment):**
```sddl
@instant_parse
Record Data(align_val) = {
  value: align(align_val) Int64LE
}
```

**Not Instant-Parse (local field alignment):**
```sddl
Record Data() = {
  align_req: UInt8,
  value: align(align_req) Int64LE  # Alignment depends on data
}
```

The first is instant-parse because alignment is known from the parameter. The second requires scanning because alignment depends on a parsed field.

---

## Summary

SDDL provides three mechanisms for controlling layout:

**`align(n)`:**

- Aligns a field to an n-byte boundary
- Adds padding before the field if needed
- Common values: 4, 8, 16, 64

**`pad_to n`:**

- Ensures a record is exactly n bytes
- Adds padding at the end
- Format error if record is naturally larger than n

**`pad_align n`:**

- Rounds record size up to next multiple of n
- Adds padding at the end
- Always succeeds (never a format error)

**Combining:**

- `pad_to` is applied first, then `pad_align`
- Both can use parameters for instant-parse

**Key Points:**

- Padding bytes are "don't care" values
- Alignment propagates through nested structures
- Parameters keep alignment instant-parse
- Local fields break instant-parse

These features describe existing binary layouts with specific alignment and padding requirements.

---

## Next Steps

- **[Conditional & Variant Data](conditional-variant.md)** - Unions and conditional fields
- **[Variables and Expressions](variables-expressions.md)** - Computing derived values
- **[Best Practices](best-practices.md)** - Guidelines for effective SDDL
- **[Real-World Formats](real-formats.md)** - Complete format descriptions
