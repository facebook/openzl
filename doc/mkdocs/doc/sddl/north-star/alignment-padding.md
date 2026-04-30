# Alignment and Padding

*Chapter 6 - Explicit control of memory layout*

Binary formats often require precise alignment rules and padding. This chapter covers SDDL's constructs for controlling layout at the field and record level: field alignment with `align(n)`, record padding with `pad_to` and `pad_align`.

For a foundational overview of SDDL language elements like records, types, and modifiers, see the [Language Elements Overview](core-concepts.md#language-elements-overview).

---

## Record Padding

SDDL provides two directives for controlling the total size of a record.

### `pad_to n` - Exact Size

`pad_to n` enforces that a record is exactly `n` bytes. If the record's natural size is less than `n`, padding is added. If it's greater, the compiler reports a format error.

```sddl
record Header() {
  magic: Bytes(4),
  version: UInt16LE,
  flags: UInt16LE
} pad_to 16
```

This `Header` is exactly 16 bytes. The natural size is 8 bytes, so 8 bytes of padding are added.

!!! danger "Format error example"
    ```sddl
    record TooBig() {
      data: Bytes(20)
    } pad_to 16  # ERROR: Record is 20 bytes, cannot pad to 16
    ```

### `pad_align n` - Round to Multiple

`pad_align n` rounds the record size up to the next multiple of `n` bytes.

```sddl
record Scanline(width) {
  pixels: UInt8[width]
} pad_align 4
```

If `width` is 10, the natural size is 10 bytes. With `pad_align 4`, the record becomes 12 bytes (the next multiple of 4).

### Combining `pad_to` and `pad_align`

When both are present, `pad_to` is applied first, then `pad_align`:

```sddl
record Block(datasize, blocksize) {
  data: Bytes(datasize)
} pad_to blocksize pad_align 8
```

This ensures the record is at least blocksize bytes, then rounds up to the next multiple of 8.
For example, if `blocksize` is 50:

1. `pad_to blocksize` makes it 50 bytes
2. `pad_align 8` rounds 50 up to 56 bytes (next multiple of 8)

### Parameterized Padding

Padding values can be parameters:

```sddl
record Container(min_size, align_to) {
  header: Bytes(16),
  payload: Bytes(100)
} pad_to min_size pad_align align_to
```

The padding depends on parameters passed when the record is instantiated. This maintains instant-parse status.

### Understanding Padding Bytes

Padding bytes are "don't care" values. SDDL doesn't specify what they contain, just that they exist:

```sddl
record Padded() {
  value: UInt32LE
} pad_to 16
```

The defined record Padded is 16 bytes: the padding bytes are included in the record size.

---

## Practical Examples

### Example 1: Hardware-Aligned Structure

```sddl
# Structure matching hardware expectations
record DeviceRegister() {
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
record DatabaseRecord() {
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
record CacheOptimized() {
  hot_data: Bytes(32),    # Frequently accessed data
  cold_data: Bytes(16)    # Less frequently accessed
} pad_align 64
```

The natural size is 48 bytes. `pad_align 64` rounds up to 64 bytes, ensuring each record fits in one cache line.

### Example 4: Format with Variable Header

```sddl
record File(header_size) {
  header_data: Bytes(header_size)
} pad_to header_size pad_align 512

size: UInt16LE
file_header: File(size)
```

The header is at least `header_size` bytes, rounded up to a 512-byte boundary (common disk sector size).

### Example 5: Array with Padding

```sddl
record Entry() {
  timestamp: Int64LE,
  value: Float32LE
} pad_align 16

entries: Entry[1000]
```

Each entry is 12 bytes naturally (8+4), but `pad_align 16` makes each 16 bytes. The array has consistent 16-byte elements with 4 bytes of padding each.

---

## Field Alignment with `align(n)`

The `align(n)` modifier specifies that a field must start at an address that is
a multiple of `n` bytes relative to the beginning of its enclosing scope (file or record).
Only power of 2 values are allowed for `n` (1, 2, 4, 8, 16, etc).

Unlike `pad_to` and `pad_align` which add padding bytes to a record's total size,
the `align(n)` modifier inserts padding *between* fields without affecting the
aligned field's own size. A field declared as `value: align(8) Int64LE` is still
8 bytes; any alignment padding comes before it but is not part of the field itself
(padding becomes part of the enclosing context).

Note that, consequently, the first field in a record is always aligned, since it
stands at position 0 relative to its enclosing scope. If you want to enforce an
alignment requirement for an entire record when it's embedded in other structures,
see "Record Alignment" below.

### Basic Syntax

```sddl
record Data() {
  flags: UInt8,
  value: align(8) Int64LE  # Starts at 8-byte boundary, relative to beginning of Data
}
```

If `flags` ends at byte 1, the `value` field will start at byte 8 (the next 8-byte boundary), leaving 7 bytes of padding between them. The padding bytes are part of `Data`, not `value` nor `flags`.

### Common Alignment Values

- `align(4)` - 4-byte alignment (common for 32-bit integers)
- `align(8)` - 8-byte alignment (common for 64-bit integers and doubles)
- `align(16)` - 16-byte alignment (SIMD register size)
- `align(64)` - 64-byte alignment (cache line size on many systems)

### Field Alignment in Records

```sddl
record Header() {
  magic: Bytes(4),
  version: UInt16LE,
  _: Bytes(2),                    # Explicit padding to 8 bytes
  timestamp: align(8) Int64LE,    # Aligned 64-bit value
  checksum: UInt32LE
}
```

The `align(8)` ensures `timestamp` starts at an 8-byte boundary, relative to the beginning of its enclosing scope, regardless of what precedes it.

### Field Alignment in Arrays

When a type has alignment requirements, arrays of that type include inter-element padding:

```sddl
record Entry() {
  id: UInt8,
  value: align(8) Float64LE
}

entries: Entry[100]
```

Each `Entry` in the array will have padding after `id` to ensure `value` is 8-byte aligned. This padding is repeated for every element.

### Field Alignment and Instant-Parse

Alignment arguments must be constant or parameter-based for instant-parse:

```sddl
record Data(align_width) {
  header: Bytes(10),
  payload: align(align_width) Bytes(100)  # OK: depends on parameter
} @instant_parse
```

If a local field must be read to determine alignment, it breaks instant-parse property of its enclosing scope:

!!! warning "Breaks instant-parse"
    ```sddl
    record Data() {
      align_req: UInt8,
      payload: align(align_req) Bytes(100)
    }
    ```
    This would error if `@instant_parse` was specified because alignment depends on a parsed field.

---

## Record Alignment

Alignment can be specified at Record level:

```sddl
record FAligned8() align(8) {
  value: Float64LE
}
```

Any field later defined using this Record definition will automatically be aligned to 8 bytes (relative to the beginning of its enclosing scope).

```sddl
record someRecord() {
  flag: UInt8,
  value: FAligned8,  # Automatically aligned to 8 bytes, relative to beginning of someRecord
}
```

---

## Summary

Use `align(n)` when individual fields must start on a boundary, `pad_to n` when an entire record must have an exact size, and `pad_align n` when you need to round record sizes up to a multiple. `pad_to` executes before `pad_align`, and both accept parameters so layouts remain instant-parse. Padding bytes are always “don’t care” values; alignment and padding propagate into nested records, and any dependency on local fields makes the construct require scanning.

---

## Where to Go Next

- **[Conditional & Variant Data](conditional-variant.md)** if alignment depends on optional fields.
- **[Variables and Expressions](variables-expressions.md)** to compute padding lengths with expressions.
- **[Real-World Formats](real-formats.md)** for alignment and padding in context.
