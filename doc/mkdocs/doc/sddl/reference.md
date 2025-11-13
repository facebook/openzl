# Quick Reference

*Chapter 11 - Quick lookup for SDDL syntax and features*

This reference provides a quick lookup for SDDL syntax, types, operators, and functions. For detailed explanations and examples, follow the chapter links.

---

## Type Reference

### Primitive Integer Types

| Type | Size | Signed | Endian | Chapter |
|------|------|--------|--------|---------|
| `Int8` | 1 byte | Yes | N/A | [Core Concepts](core-concepts.md#primitive-types) |
| `Int16LE`, `Int16BE` | 2 bytes | Yes | Little/Big | [Core Concepts](core-concepts.md#primitive-types) |
| `Int32LE`, `Int32BE` | 4 bytes | Yes | Little/Big | [Core Concepts](core-concepts.md#primitive-types) |
| `Int64LE`, `Int64BE` | 8 bytes | Yes | Little/Big | [Core Concepts](core-concepts.md#primitive-types) |
| `UInt8` | 1 byte | No | N/A | [Core Concepts](core-concepts.md#primitive-types) |
| `UInt16LE`, `UInt16BE` | 2 bytes | No | Little/Big | [Core Concepts](core-concepts.md#primitive-types) |
| `UInt32LE`, `UInt32BE` | 4 bytes | No | Little/Big | [Core Concepts](core-concepts.md#primitive-types) |
| `UInt64LE`, `UInt64BE` | 8 bytes | No | Little/Big | [Core Concepts](core-concepts.md#primitive-types) |

**Note:** All multi-byte types must explicitly specify endianness (`LE` or `BE`).

### Floating-Point Types

| Type | Size | Format | Endian | Chapter |
|------|------|--------|--------|---------|
| `Float16LE`, `Float16BE` | 2 bytes | IEEE 754 | Little/Big | [Core Concepts](core-concepts.md#primitive-types) |
| `Float32LE`, `Float32BE` | 4 bytes | IEEE 754 | Little/Big | [Core Concepts](core-concepts.md#primitive-types) |
| `Float64LE`, `Float64BE` | 8 bytes | IEEE 754 | Little/Big | [Core Concepts](core-concepts.md#primitive-types) |
| `BFloat16LE`, `BFloat16BE` | 2 bytes | Google bfloat16 | Little/Big | [Core Concepts](core-concepts.md#primitive-types) |

### Raw Bytes

| Type | Description | Chapter |
|------|-------------|---------|
| `Bytes(n)` | Fixed-size byte array of length `n` | [Core Concepts](core-concepts.md#bytes-type) |
| `Bytes until delim` | Variable-size bytes until delimiter | [Core Concepts](core-concepts.md#delimiter-based-parsing) |

---

## Syntax Quick Reference

### Records

```sddl
Record TypeName(param1, param2) = {
  field1: Type1,
  field2: Type2(param1)
} pad_to size pad_align boundary
```

**See:** [Core Concepts - Records](core-concepts.md#records)

### Unions

```sddl
Union TypeName(selector, param) = {
  case 1: Type1(param),
  case 2, 3: Type2(param),
  case 4..10: Type3(param),
  default: Type4(param)
}
```

**See:** [Conditional & Variant Data - Unions](conditional-variant.md#unions-for-variant-data)

### Enumerations

```sddl
enum EnumName {
  VALUE1 = 1,
  VALUE2 = 2,
  VALUE3 = 1 << 2
}
```

**See:** [Conditional & Variant Data - Enumerations](conditional-variant.md#enumerations)

### Conditional Fields

**Single field:**
```sddl
when condition then field: Type
```

**Block form:**
```sddl
when condition {
  field1: Type1,
  field2: Type2,
  var x = field1 + field2
}
```

**See:** [Conditional & Variant Data - Conditional Fields](conditional-variant.md#conditional-fields-with-when)

### Arrays

```sddl
fixed: Type[count]                    # Fixed-size
multi: Type[height][width]            # Multi-dimensional
auto: Type[]                          # Auto-sized (to end of scope)
soa_array: soa Type[count]            # Structure-of-arrays
```

**See:** [Arrays & Collections](arrays-collections.md)

### Variables

```sddl
var name = expression
```

**See:** [Variables & Expressions](variables-expressions.md#the-var-statement)

### Validation

```sddl
expect condition                      # Standalone validation
field: Type where (condition)         # Inline validation
```

**See:** [Variables & Expressions](variables-expressions.md#validation)

### Switch Expressions

```sddl
var value = switch selector {
  case 1: result1,
  case 2, 3: result2,
  case 4..10: result3,
  default: result4
}
```

**Rules:**
- All cases must return the same type
- Overlapping ranges cause a format error
- No match without `default` causes a data error

**See:** [Variables & Expressions - Switch Expressions](variables-expressions.md#switch-expressions)

---

## Operator Reference

### Arithmetic Operators

| Operator | Description | Example | Chapter |
|----------|-------------|---------|---------|
| `+` | Addition | `a + b` | [Variables & Expressions](variables-expressions.md#integer-arithmetic) |
| `-` | Subtraction | `a - b` | [Variables & Expressions](variables-expressions.md#integer-arithmetic) |
| `*` | Multiplication | `a * b` | [Variables & Expressions](variables-expressions.md#integer-arithmetic) |
| `/` | Division | `a / b` | [Variables & Expressions](variables-expressions.md#integer-arithmetic) |
| `%` | Modulo | `a % b` | [Variables & Expressions](variables-expressions.md#integer-arithmetic) |

**Note:** All arithmetic is 64-bit signed. Overflow or division by zero causes a format error.

### Bitwise Operators

| Operator | Description | Example | Chapter |
|----------|-------------|---------|---------|
| `&` | Bitwise AND | `flags & 0x01` | [Variables & Expressions](variables-expressions.md#bitwise-operations) |
| `\|` | Bitwise OR | `a \| b` | [Variables & Expressions](variables-expressions.md#bitwise-operations) |
| `^` | Bitwise XOR | `a ^ b` | [Variables & Expressions](variables-expressions.md#bitwise-operations) |
| `<<` | Left shift | `value << 8` | [Variables & Expressions](variables-expressions.md#bitwise-operations) |
| `>>` | Right shift | `value >> 8` | [Variables & Expressions](variables-expressions.md#bitwise-operations) |

### Comparison Operators

| Operator | Description | Example | Chapter |
|----------|-------------|---------|---------|
| `==` | Equal | `a == b` | [Variables & Expressions](variables-expressions.md#comparisons) |
| `!=` | Not equal | `a != b` | [Variables & Expressions](variables-expressions.md#comparisons) |
| `<` | Less than | `a < b` | [Variables & Expressions](variables-expressions.md#comparisons) |
| `<=` | Less or equal | `a <= b` | [Variables & Expressions](variables-expressions.md#comparisons) |
| `>` | Greater than | `a > b` | [Variables & Expressions](variables-expressions.md#comparisons) |
| `>=` | Greater or equal | `a >= b` | [Variables & Expressions](variables-expressions.md#comparisons) |
| `in` | Enum membership | `value in EnumType` | [Conditional & Variant Data](conditional-variant.md#enum-membership-testing-with-in) |

### Logical Operators

| Operator | Description | Example | Chapter |
|----------|-------------|---------|---------|
| `and` | Logical AND | `a and b` | [Variables & Expressions](variables-expressions.md#logical-operations) |
| `or` | Logical OR | `a or b` | [Variables & Expressions](variables-expressions.md#logical-operations) |
| `not` | Logical NOT | `not a` | [Variables & Expressions](variables-expressions.md#logical-operations) |

### Operator Precedence

SDDL follows C11 operator precedence. Use parentheses for clarity.

**See:** [Variables & Expressions - Operator Precedence](variables-expressions.md#operator-precedence)

---

## Function Reference

### Mathematical Functions

| Function | Description | Returns | Chapter |
|----------|-------------|---------|---------|
| `abs(x)` | Absolute value of x | 64-bit signed int | [Variables & Expressions](variables-expressions.md#mathematical-functions) |
| `min(a, b)` | Minimum of a and b | 64-bit signed int | [Variables & Expressions](variables-expressions.md#mathematical-functions) |
| `max(a, b)` | Maximum of a and b | 64-bit signed int | [Variables & Expressions](variables-expressions.md#mathematical-functions) |
| `clamp(l, x, h)` | Clamp x to range [l, h] | 64-bit signed int | [Variables & Expressions](variables-expressions.md#mathematical-functions) |
| `sgn(x)` | Sign of x (-1, 0, or 1) | 64-bit signed int | [Variables & Expressions](variables-expressions.md#mathematical-functions) |
| `between(l, x, h)` | True if l ≤ x ≤ h | Boolean (0 or 1) | [Variables & Expressions](variables-expressions.md#mathematical-functions) |

### Alignment and Division

| Function | Description | Returns | Chapter |
|----------|-------------|---------|---------|
| `ceil_div(x, d)` | Ceiling division ⌈x / d⌉ | 64-bit signed int | [Variables & Expressions](variables-expressions.md#alignment-and-division) |
| `align_up(x, a)` | Round x up to multiple of a | 64-bit signed int | [Variables & Expressions](variables-expressions.md#alignment-and-division) |

### Size and Position Functions

| Function | Description | Instant-Parse? | Chapter |
|----------|-------------|----------------|---------|
| `sizeof(T())` | Static size of type T | Yes | [Variables & Expressions](variables-expressions.md#size-and-position-functions) |
| `size(field)` | Parsed byte size of field | No (requires scan) | [Variables & Expressions](variables-expressions.md#size-and-position-functions) |
| `current_position()` | Current parser position | No (requires scan) | [Variables & Expressions](variables-expressions.md#size-and-position-functions) |
| `scope_remaining()` | Bytes remaining in scope | No (requires scan) | [Variables & Expressions](variables-expressions.md#size-and-position-functions) |

---

## Padding and Alignment

### Record-Level Modifiers

| Modifier | Description | Chapter |
|----------|-------------|---------|
| `pad_to n` | Extend record to exactly n bytes | [Alignment & Padding](alignment-padding.md#pad-to) |
| `pad_align n` | Round record size up to multiple of n | [Alignment & Padding](alignment-padding.md#pad-align) |

Both can be combined: `pad_to` is applied first, then `pad_align`.

### Field-Level Alignment

```sddl
aligned_field: align(16) Type
```

**See:** [Alignment & Padding - Field Alignment](alignment-padding.md#field-alignment)

---

## Annotations

| Annotation | Purpose | Chapter |
|------------|---------|---------|
| `@instant_parse` | Enforce instant-parse requirement | [Instant-Parse Model](instant-parse.md#enforcing-instant-parse) |
| `@err_msg "text"` | Custom error message for validation | [Best Practices](best-practices.md) |
| `@chunk_size` | Hint for chunk processing | [sddl-for-llm](sddl-for-llm.md#annotations) |

**See:** [sddl-for-llm - Annotations](sddl-for-llm.md#annotations)

---

## Instant-Parse Model

A type is **instant-parse** if all field offsets and sizes can be computed from parameters and constants alone (no dependency on parsed data).

### What Breaks Instant-Parse

- Field size/offset depends on previously parsed field
- Using `Bytes until` (delimiter-based parsing)
- `expect` or `where` referencing local fields
- Using `current_position()` or state-dependent functions
- Auto-sized arrays (`Type[]`)

### Instant-Parse Rules

| Construct | Instant-Parse When... |
|-----------|----------------------|
| Record | All fields are instant-parse and no local field dependencies |
| Union | Selector is parameter/constant AND all cases are instant-parse |
| Array | Size is parameter/constant AND element type is instant-parse |
| Variable | References only parameters/constants |
| Conditional | Condition uses only parameters/constants |

**See:** [Instant-Parse Model](instant-parse.md)

---

## Common Patterns

### Magic Number Validation

```sddl
magic: Bytes(4)
expect magic == "RIFF"
```

**See:** [Real-World Formats - Common Patterns](real-formats.md#common-patterns-from-these-examples)

### Length-Prefixed Data

```sddl
length: UInt32LE
data: Bytes(length)
```

### Flag Extraction

```sddl
flags: UInt8
var has_feature = (flags & 0x01) != 0
when has_feature then feature_data: FeatureData
```

**See:** [Variables & Expressions - Flag Extraction](variables-expressions.md#example-2-flag-extraction)

### Version-Based Layout

```sddl
version: UInt16LE
when version >= 2 then extended_fields: ExtendedData
```

**See:** [Conditional & Variant Data - Version-Specific Fields](conditional-variant.md#pattern-version-specific-fields)

### Array Size Computation

```sddl
width: UInt32LE
height: UInt32LE
var num_pixels = width * height
pixels: Pixel[num_pixels]
```

**See:** [Variables & Expressions - Computing Array Sizes](variables-expressions.md#example-1-computing-array-sizes)

---

## Error Types

| Error Type | When It Occurs | Example |
|------------|----------------|---------|
| **Format Error** | Structural contradiction | Overlapping union cases, `pad_to` too small, overflow |
| **Data Error** | Data doesn't match expected structure | Failed `expect`, missing delimiter, unmatched union case |
| **Instant-Parse Violation** | `@instant_parse` requirement not met | Field depends on parsed data |

**See:** [sddl-for-llm - Error Classes](sddl-for-llm.md#error-classes)

---

## Keywords

**Reserved:** `Record`, `Union`, `enum`, `when`, `then`, `case`, `default`, `var`, `expect`, `where`, `scan`, `soa`, `align`, `pad_to`, `pad_align`, `until`, `include_delim`, `switch`, `and`, `or`, `not`, `in`

**See:** [Getting Started - Basic Syntax](getting-started.md#basic-syntax-rules)

---

## Complete Documentation

For comprehensive learning and detailed explanations:

1. **[Introduction](introduction.md)** - What is SDDL and why use it?
2. **[Getting Started](getting-started.md)** - Your first SDDL description
3. **[Core Concepts](core-concepts.md)** - Types, records, and basic structures
4. **[Arrays & Collections](arrays-collections.md)** - Working with arrays
5. **[Instant-Parse Model](instant-parse.md)** - Performance and layout
6. **[Alignment & Padding](alignment-padding.md)** - Memory layout control
7. **[Conditional & Variant Data](conditional-variant.md)** - Unions and conditionals
8. **[Variables & Expressions](variables-expressions.md)** - Computing values
9. **[Best Practices](best-practices.md)** - Guidelines for effective SDDL
10. **[Real-World Formats](real-formats.md)** - Complete examples (WAV, BMP)
11. **[sddl-for-llm](sddl-for-llm.md)** - Complete technical specification
