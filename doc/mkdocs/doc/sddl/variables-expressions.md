# Variables and Expressions

*Chapter 8 - Computing derived values*

SDDL allows computing derived values during parsing using variables and expressions. This chapter covers the `var` statement, expression syntax, standard functions, and how these features interact with instant-parse.

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
@instant_parse
Record Data(total_size) = {
  var payload_size = total_size - 16,  # OK: depends on parameter
  header: Bytes(16),
  payload: Bytes(payload_size)
}
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

when header.has_checksum then checksum: UInt32LE
when header.is_compressed then compression_info: CompressionHeader
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
  when has_extended then extended: ExtendedData
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

  when has_payload then payload: Bytes(payload_size)
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

**Variables:**

- Declared with `var name = expression`
- Immutable once created
- Scoped to containing record or top-level
- Instant-parse when depending on parameters/constants only

**Expressions:**

- 64-bit signed integer arithmetic
- Bitwise operations for flag manipulation
- Comparison and logical operators
- C11 operator precedence

**Switch Expressions:**

- Multi-way value selection
- Support literals, multiple values, ranges
- Require `default` to avoid data errors

**Standard Functions:**

- Mathematical: `abs`, `min`, `max`, `clamp`, `sgn`
- Range checking: `between`
- Alignment: `ceil_div`, `align_up`
- Size/position: `sizeof` (instant-parse), `size`, `current_position`, `scope_remaining` (require scan)

**Error Handling:**

- Overflow causes format error
- Division by zero causes format error

**Key Insight:**
Variables and expressions referencing parameters or constants maintain instant-parse status. References to parsed fields require scanning.

---

## Next Steps

- **[Best Practices](best-practices.md)** - Guidelines for effective SDDL
- **[Real-World Formats](real-formats.md)** - Complete format examples
- **[Reference](reference.md)** - Complete language reference
