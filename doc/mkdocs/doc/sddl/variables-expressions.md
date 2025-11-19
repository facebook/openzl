# Variables and Expressions

*Chapter 6 - Computing derived values during parsing*

SDDL allows computing derived values during parsing using variables and expressions. This chapter covers the `var` statement, expression syntax, standard functions, and how these features interact with instant-parse.

For an overview of how variables and expressions fit into SDDL's type system, see the [Language Elements Overview](core-concepts.md#language-elements-overview).

Need a concrete spec that leans on these tools? Jump to the [coverage map entry for derived values](real-formats.md#coverage-var).

---

## Referencing Fields

When SDDL parses a field from the binary data, it creates a binding between the field name and the parsed value. You can reference these field values in subsequent expressions, enabling dynamic behavior based on the data itself.

### Basic Field References

Every field you parse creates a name that can be referenced later:

```sddl
Record Header() = {
  magic: Bytes(4),
  version: Int16LE,
  count: Int32LE
}

header: Header

# After parsing 'header', you can reference its fields
items: Item[header.count]        # Use count for array size
expect header.version >= 2       # Use version in validation
```

The binding happens immediately after the field is parsed, making its value available to all subsequent statements in the same scope. Use dot notation to access fields within nested records—each dot traverses one level deeper into the structure.

### Type Restrictions for Expressions

**IMPORTANT:** Not all field types can be used in expressions. SDDL's expression engine operates on 64-bit signed integers, which covers the vast majority of use cases (array sizes, offsets, counts, flags) while keeping the type system simple and predictable.

**Supported in expressions:** All integer types (Int8, Int16LE, Int32BE, UInt32LE, etc.) up to signed 64-bit integers (Int64LE, Int64BE).

**Not supported in expressions:** Unsigned 64-bit integers (UInt64LE, UInt64BE), all floating-point types (Float16LE/BE, Float32LE/BE, Float64LE/BE, BFloat16LE/BE), and byte sequences (Bytes).

You can still **parse** any type—these restrictions only apply to using field values in expressions:

```sddl
Record Data() = {
  timestamp: UInt64LE,      # ✓ Can parse
  temperature: Float32LE,   # ✓ Can parse

  # But cannot use in expressions:
  # var x = timestamp + 100        # ✗ ERROR: UInt64LE not supported
  # when temperature > 20.0 ...    # ✗ ERROR: Float not supported

  count: Int32LE,
  items: Item[count]        # ✓ OK: Int32LE works in expressions
}
```

Workarounds: For validation, use `expect` statements on fields directly without arithmetic. For conditionals, pre-determine behavior based on format version or flags. For sizes, prefer Int32LE or Int64LE over UInt64LE.

### Common Uses for Field References

Field references appear throughout SDDL specifications in array sizes (`items: Item[count]`), record parameters (`Block(header.size)`), byte counts (`Bytes(length)`), conditional fields (`when (flags & 0x01) != 0 { ... }`), validation statements (`expect magic == "RIFF"`), variable expressions (`var total = width * height`), and switch expressions.

### Scope Rules

You can reference fields that are in the same scope (same record or top-level), from a previously parsed field using dot notation, or passed as parameters to a record. You cannot reference fields from outer scopes unless they're passed as parameters:

```sddl
header_count: Int32LE

Record Erroneous_Data() = {
  # ERROR: Cannot reference top-level 'header_count' directly
  # items: Item[header_count]
}

# CORRECT: Pass it as a parameter
Record Data(count) = {
  items: Item[count]
}

data: Data(header_count)
```

Fields within the same record can reference each other directly (`data: Bytes(size)` where `size` is a field in the same record), though this requires scanning rather than instant-parse.

### Performance Impact

Field references affect instant-parse status based on what you reference. Parameters are instant-parse safe, while references to local parsed fields require sequential scanning. See [Understanding Instant-Parse](instant-parse.md) for performance implications.

---

## The `var` Statement

Variables store computed values for later use.

### Basic Syntax

```sddl
header: Header
var data_size = header.size - 16
payload: Bytes(data_size)
```

Variables are declared with `var`, followed by a name, `=`, and an expression.

### Immutability

Variables are immutable once created:

```sddl
var count = header.count
# count = count + 1  # ERROR: Cannot modify variable
```

This ensures predictable behavior and simplifies analysis.

### Scope

Variables are scoped to the record or top-level context where they're defined:

```sddl
Record Container() = {
  size: UInt32LE,
  var payload_size = size - 8,  # Scoped to Container
  payload: Bytes(payload_size)
}

# payload_size not accessible here
```

### Variables and Instant-Parse

Variables referencing parameters or constants are instant-parse safe:

```sddl
Record Data(total_size) = {
  var payload_size = total_size - 16,  # OK: depends on parameter
  header: Bytes(16),
  payload: Bytes(payload_size)
} @instant_parse
```

Variables referencing parsed fields require scanning:

```sddl
Record Data() = {
  size: UInt32LE,
  var payload_size = size - 16,  # Requires scan: depends on parsed field
  payload: Bytes(payload_size)
}
```

---

## Expressions

Expressions compute values from fields, parameters, variables, and constants.

### Integer Arithmetic

All integers are 64-bit signed. Standard operators:

```sddl
var total = width * height
var aligned = (size + 15) / 16 * 16
var offset = base + index * element_size
```

Operators: `+`, `-`, `*`, `/`, `%` (modulo)

### Bitwise Operations

Extract and manipulate bits:

```sddl
var has_flag = (flags & 0x01) != 0
var masked = value & 0xFF
var shifted = (data >> 8) & 0xFF
```

Operators: `&` (AND), `|` (OR), `^` (XOR), `<<` (left shift), `>>` (right shift)

### Comparisons

Produce boolean values for conditions:

```sddl
var is_valid = version >= 2
var in_range = size > 0 and size <= 1024
```

Operators: `==`, `!=`, `<`, `<=`, `>`, `>=`

### Logical Operations

Combine boolean values:

```sddl
var has_both = (flags & 0x01) != 0 and (flags & 0x02) != 0
var has_either = mode == 1 or mode == 2
var is_disabled = not enabled
```

Operators: `and`, `or`, `not`

### Operator Precedence

SDDL follows C11 operator precedence. Use parentheses for clarity:

```sddl
var result = (a + b) * c  # Clear: add first, then multiply
var flags = (value & 0xFF) | (type << 8)  # Clear grouping
```

---

## Switch Expressions

Compute values based on multi-way selection:

```sddl
var block_size = switch version {
  case 1: 512,
  case 2, 3: 1024,
  case 4..10: 2048,
  default: 4096
}
```

Rules:
- All cases must return the same type
- Overlapping ranges cause a format error
- Without `default`, unmatched values cause a data error

Using with variables:

```sddl
Record File() = {
  version: UInt16LE,

  var chunk_size = switch version {
    case 1: 512,
    case 2: 1024,
    default: 2048
  },

  chunks: Chunk(chunk_size)[]
}
```

---

## Standard Functions

SDDL provides built-in functions for common operations. All functions are pure (no side effects) and return 64-bit signed integers.

### Mathematical Functions

```sddl
abs(x)              # Absolute value
min(a, b)           # Minimum of two values
max(a, b)           # Maximum of two values
clamp(l, x, h)      # Clamp x to range [l, h]
sgn(x)              # Sign: -1, 0, or 1
between(l, x, h)    # True if l <= x <= h
```

### Alignment and Division

```sddl
ceil_div(x, d)      # Ceiling division: ⌈x / d⌉
align_up(x, a)      # Round x up to next multiple of a
```

### Size and Position Functions

```sddl
sizeof(T())               # Static size of type T (instant-parse)
size(field)               # Parsed byte size of field (requires scan)
current_position()        # Current parser position (requires scan)
scope_remaining()         # Bytes remaining in scope (requires scan)
```

### Notes

- All arithmetic is checked for overflow and division by zero (both cause format errors)
- Functions referencing parsed data (`size`, `current_position`, `scope_remaining`) require scanning
- `sizeof` is instant-parse because it computes static type sizes

---

## Practical Examples

### Example 1: Computing Array Sizes

```sddl
Record Image() = {
  width: UInt32LE,
  height: UInt32LE,
  channels: UInt8,

  var num_pixels = width * height,
  var pixel_size = channels,
  var total_bytes = num_pixels * pixel_size,

  pixels: UInt8[total_bytes]
}
```

### Example 2: Flag Extraction

```sddl
Record Header() = {
  flags: UInt16LE,

  var has_checksum = (flags & 0x01) != 0,
  var is_compressed = (flags & 0x02) != 0,
  var version = (flags >> 8) & 0xFF
}

header: Header

when header.has_checksum { checksum: UInt32LE }
when header.is_compressed { compression_info: CompressionHeader }
```

### Example 3: Version-Based Sizes

```sddl
Record Config() = {
  version: UInt16LE,

  var header_size = switch version {
    case 1: 32,
    case 2: 64,
    case 3: 128,
    default: 256
  },

  var has_extended = version >= 3,

  header: Bytes(header_size),
  when has_extended { extended: ExtendedData }
}
```

### Example 4: Alignment Calculations

```sddl
Record Block() = {
  size: UInt32LE,

  var aligned_size = align_up(size, 16),
  var padding_needed = aligned_size - size,

  data: Bytes(size),
  padding: Bytes(padding_needed)
}
```

### Example 5: Conditional Payload Size

```sddl
Record Packet() = {
  header: PacketHeader,

  var payload_size = header.total_size - sizeof(PacketHeader()),
  var has_payload = payload_size > 0,

  when has_payload { payload: Bytes(payload_size) }
}
```

### Example 6: Bit Field Extraction

```sddl
Record Descriptor() = {
  packed: UInt32LE,

  var type = packed & 0xFF,
  var flags = (packed >> 8) & 0xFF,
  var count = (packed >> 16) & 0xFFFF,

  items: Item[count]
}
```


---

## Summary

Variables let you capture derived values or parameters for later use; they are immutable and stay instant-parse as long as they depend only on parameters or constants. Expressions follow 64-bit signed arithmetic rules, include bitwise/logical operators with C11 precedence, and can be organized via switch expressions when multi-way selection is needed. Standard functions cover math, range checks, and alignment helpers; `sizeof` works only for instant-parse constructs, while `size(field)` and position helpers require scanning. Overflow and division-by-zero remain format errors, so guard derived values accordingly.

---

## Where to Go Next

- **[Best Practices](best-practices.md)** to see how validation and expressions interact in full specs.
- **[Real-World Formats](real-formats.md)** for examples that combine variables with complex layouts.
- **[Reference](reference.md)** when you need a concise lookup for syntax and functions.
