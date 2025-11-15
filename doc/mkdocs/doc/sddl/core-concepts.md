# Core Concepts

*Chapter 3 - The fundamental building blocks*

This chapter provides a comprehensive look at SDDL's core features: types, records, validation, and the lexical structure of the language. While Chapter 2 introduced these concepts through examples, this chapter explores them in detail.

---

## Types and Endianness

SDDL provides a small set of primitive types for describing binary data. Every type is designed to have clear, unambiguous semantics.

### Integer Types

SDDL supports signed and unsigned integers in multiple sizes:

**Signed Integers:**

- `Int8` - 8-bit signed integer (-128 to 127)
- `Int16LE` / `Int16BE` - 16-bit signed integer, little/big-endian
- `Int32LE` / `Int32BE` - 32-bit signed integer, little/big-endian
- `Int64LE` / `Int64BE` - 64-bit signed integer, little/big-endian

**Unsigned Integers:**

- `UInt8` - 8-bit unsigned integer (0 to 255)
- `UInt16LE` / `UInt16BE` - 16-bit unsigned integer, little/big-endian
- `UInt32LE` / `UInt32BE` - 32-bit unsigned integer, little/big-endian
- `UInt64LE` / `UInt64BE` - 64-bit unsigned integer, little/big-endian

**Example:**
```sddl
count: UInt32LE      # Unsigned 32-bit integer, little-endian
offset: Int64BE      # Signed 64-bit integer, big-endian
flags: UInt8         # 8-bit unsigned (no endianness needed)
```

### Floating-Point Types

SDDL supports IEEE 754 floating-point types and Google's bfloat16:

**IEEE 754 Standard:**
- `Float16LE` / `Float16BE` - 16-bit IEEE 754 half-precision
- `Float32LE` / `Float32BE` - 32-bit IEEE 754 single-precision
- `Float64LE` / `Float64BE` - 64-bit IEEE 754 double-precision

**Google bfloat16:**
- `BFloat16LE` / `BFloat16BE` - 16-bit brain floating-point format

**Example:**
```sddl
temperature: Float32LE    # Single-precision, little-endian
position: Float64BE       # Double-precision, big-endian
ml_weight: BFloat16LE     # Brain float, commonly used in ML
```

### The Bytes Type

For untyped data or data with unknown structure, use `Bytes(n)`:

```sddl
magic: Bytes(4)           # 4 bytes of data
header: Bytes(128)        # 128-byte header
padding: Bytes(16)        # 16 bytes of padding
```

The argument to `Bytes` specifies the number of bytes. It can be:
- A constant: `Bytes(100)`
- A parameter: `Bytes(size)` where `size` is a record parameter
- An expression: `Bytes(header.length - 4)`

### Why Explicit Endianness?

SDDL requires every multi-byte type to declare its byte order. This design decision prevents a common class of bugs:

```sddl
# This is an ERROR - no endianness specified
value: Int32  # Compiler rejects this
```

```sddl
# This is correct - endianness is explicit
value: Int32LE  # Little-endian
```

This design prevents endianness bugs, which are common and frustrating in binary format work. You can't accidentally read big-endian data as little-endian because the type system won't let you forget to specify byte order. The byte order is visible right at each field—you don't need to look elsewhere for a global setting or guess from context. Every field documents its own intent clearly.

### Single-Byte Types

`Int8` and `UInt8` don't have endianness suffixes because single bytes have no byte order:

```sddl
byte_value: UInt8     # No LE/BE needed
signed_byte: Int8     # No LE/BE needed
```

### Type Summary Table

| Type | Size | Endian | Range |
|------|------|--------|-------|
| `Int8` | 1 byte | N/A | -128 to 127 |
| `UInt8` | 1 byte | N/A | 0 to 255 |
| `Int16LE/BE` | 2 bytes | Yes | -32,768 to 32,767 |
| `UInt16LE/BE` | 2 bytes | Yes | 0 to 65,535 |
| `Int32LE/BE` | 4 bytes | Yes | -2³¹ to 2³¹-1 |
| `UInt32LE/BE` | 4 bytes | Yes | 0 to 2³²-1 |
| `Int64LE/BE` | 8 bytes | Yes | -2⁶³ to 2⁶³-1 |
| `UInt64LE/BE` | 8 bytes | Yes | 0 to 2⁶⁴-1 |
| `Float16LE/BE` | 2 bytes | Yes | IEEE 754 half |
| `Float32LE/BE` | 4 bytes | Yes | IEEE 754 single |
| `Float64LE/BE` | 8 bytes | Yes | IEEE 754 double |
| `BFloat16LE/BE` | 2 bytes | Yes | Google bfloat16 |
| `Bytes(n)` | n bytes | N/A | Raw data |

---

## Records

Records are SDDL's primary mechanism for organizing structure and reusing format definitions.

### Defining Records

A record definition has three parts: name, parameters, and body.

```sddl
Record Name(param1, param2) = {
  field1: Type1,
  field2: Type2
}
```

**Name:** Starts with a capital letter by convention (but not required). Should be descriptive.

**Parameters:** Optional. Values passed in when the record is instantiated.

**Body:** A sequence of field declarations, comma-separated.

### Simple Records

The simplest record has no parameters:

```sddl
Record Point() = {
  x: Float32LE,
  y: Float32LE,
  z: Float32LE
}

origin: Point
```

This defines a 3D point structure. When you write `origin: Point`, you're creating an instance of the `Point` record.

### Parameterized Records

Parameters make records flexible:

```sddl
Record FixedArray(count) = {
  values: Int32LE[count]
}

size: UInt32LE
data: FixedArray(size)
```

The `count` parameter is passed when instantiating the record. Parameters can be:

- Used in array sizes: `values: Int32LE[count]`
- Used in `Bytes` sizes: `data: Bytes(count)`
- Used in expressions: `data: Bytes(count * 2)`
- Passed to nested records: `nested: SubRecord(count)`
- Used in conditions: `when count > 0 then ...`

### Multiple Parameters

Records can have multiple parameters:

```sddl
Record Matrix(rows, cols) = {
  data: Float32LE[rows * cols]
}

Record Container(width, height, depth) = {
  dimensions: Matrix(width, height),
  volume: Float32LE[depth]
}

w: UInt32LE
h: UInt32LE
d: UInt32LE
container: Container(w, h, d)
```

Parameters are positional. When calling `Container(w, h, d)`, `w` maps to `width`, `h` to `height`, and `d` to `depth`.

### Fields and Field Names

Field names must be unique within a record, with one exception: the underscore `_` can be used multiple times for throwaway fields:

```sddl
Record Data() = {
  important: Int32LE,
  _: Bytes(4),          # Padding, ignored
  value: Float32LE,
  _: Bytes(4),          # More padding, also ignored
  count: Int32LE
}
```

Use `_` when a field exists in the binary format but you don't need to reference it later.

**Field name rules:**

- Must start with a letter or underscore
- Can contain letters, numbers, and underscores
- Are case-sensitive (`count` and `Count` are different)
- Should be descriptive (`temperature` is better than `val`)

### Nested Records

Records can contain other records:

```sddl
Record Point() = {
  x: Float32LE,
  y: Float32LE
}

Record Line() = {
  start: Point,
  end: Point
}

Record Shape() = {
  boundary: Line,
  center: Point
}

shape: Shape
```

This creates a hierarchy: `Shape` contains a `Line` and a `Point`, and `Line` contains two `Point`s.

### Inline Records

You can define records inline without giving them a name:

```sddl
header: Record {
  magic: Bytes(4),
  version: Int16LE
}

data: Record {
  count: Int32LE,
  values: Float32LE[10]
}
```

Inline records are useful for one-off structures that won't be reused.

### Record Scope and Variables

Records create a scope. Variables defined inside a record are local to that record:

```sddl
Record Container() = {
  size: Int32LE,
  var actual_size = size - 4,  # Local variable
  data: Bytes(actual_size)
}
```

The variable `actual_size` exists only within the `Container` record. It cannot be referenced outside.

### The `sizeof` Function

You can query the size of a record at compile time:

```sddl
Record Header() = {
  magic: Bytes(4),
  version: Int16LE,
  flags: Int16LE
}

expect sizeof(Header) == 8
```

This is useful for validation, especially when the format specification includes size fields that must match the actual structure size.

---

## Validation

SDDL provides two mechanisms for validating that binary data matches expectations: `expect` statements and `where` clauses.

### The `expect` Statement

`expect` statements assert that a condition must be true:

```sddl
header: Record {
  magic: Bytes(4),
  version: Int16LE
}

expect header.magic == "MYFT"
expect header.version >= 1
expect header.version <= 3
```

When the SDDL interpreter encounters an `expect` statement, it evaluates the condition. If the condition is false, parsing fails with a data error.

**Evaluation timing:** `expect` statements are evaluated as soon as all their dependencies are available. In the example above, both `expect` statements are evaluated immediately after parsing the `header`.

### Compound Conditions

You can combine conditions with logical operators:

```sddl
expect header.version >= 1 and header.version <= 3
expect header.magic == "MYFT" or header.magic == "MYMT"
expect !(header.flags & 0x80)  # High bit must not be set
```

### Comparing with Byte Arrays

Magic numbers and identifiers are often byte sequences:

```sddl
magic: Bytes(4)
expect magic == [0x50, 0x4B, 0x03, 0x04]  # ZIP file signature
```

You can also use string literals for ASCII:

```sddl
magic: Bytes(4)
expect magic == "RIFF"  # RIFF file format
```

String literals are treated as byte sequences in expect statements.

### The `where` Clause

`where` is a shorthand for validating a field immediately after parsing it:

```sddl
Record Data() = {
  size: UInt16LE where (size <= 1024),
  data: Bytes(size)
}
```

This is equivalent to:

```sddl
Record Data() = {
  size: UInt16LE,
  expect size <= 1024,
  data: Bytes(size)
}
```

Use `where` when validation is tightly coupled to a single field. Use `expect` when validation involves multiple fields or complex logic.

### Validation and Instant-Parse

When `expect` or `where` references only parameters or constants, it doesn't affect instant-parse status:

```sddl
@instant_parse
Record Data(max_size) = {
  expect max_size <= 1024,  # OK: depends on parameter
  data: Bytes(max_size)
}
```

When validation references local fields, the record requires scanning:

```sddl
Record Data() = {
  size: UInt16LE,
  expect size <= 1024,  # This makes the record require scanning
  data: Bytes(size)
}
```

The distinction: parameters are known before parsing, local fields are discovered during parsing.

### Error Messages

SDDL doesn't currently support custom error messages in `expect` statements (this may change). When validation fails, the interpreter reports which expect statement failed and what values were involved.

---

## Comments and Documentation

Comments in SDDL start with `#` and continue to the end of the line:

```sddl
# This is a full-line comment
magic: Bytes(4)  # This is an end-of-line comment
```

### Documenting Your Format

Good SDDL specifications are self-documenting, but comments add valuable context:

```sddl
Record Header() = {
  # File identifier, must be "MYFT" for this format version
  magic: Bytes(4),

  # Format version number
  # Version 1: Basic format
  # Version 2: Added compression support
  # Version 3: Added metadata section
  version: Int16LE,

  # Bit flags controlling optional features
  # Bit 0: Has metadata section
  # Bit 1: Data is compressed
  # Bit 2: Has checksum
  flags: Int16LE
}
```

### Comment Style Guidelines

**Field documentation:**
```sddl
temperature: Float32LE  # In degrees Celsius
count: UInt32LE         # Number of data points
offset: Int64LE         # Byte offset from start of file
```

**Section separation:**
```sddl
# ===== Header Section =====
magic: Bytes(4)
version: Int16LE

# ===== Data Section =====
count: UInt32LE
data: Int32LE[count]
```

**Explaining complex logic:**
```sddl
var has_metadata = (header.flags & 0x01) != 0
var is_compressed = (header.flags & 0x02) != 0

# Metadata is optional based on flag bit 0
when has_metadata then metadata: Metadata

# If compressed, read compressed size first
when is_compressed {
  compressed_size: UInt32LE,
  data: Bytes(compressed_size)
}
```

### Comments as Format Documentation

Your SDDL file IS the format documentation. Well-commented SDDL is often clearer than separate documentation because it shows the exact structure:

```sddl
# SAO Star Catalog Format
#
# The Smithsonian Astrophysical Observatory (SAO) star catalog
# is a binary format containing astronomical data for stars.
#
# File structure:
# - 28-byte fixed header
# - Variable-length star entries (28 bytes each in this version)

Record StarEntry() = {
  SRA0:  Float64LE,  # Right Ascension at epoch 1950.0 (radians)
  SDEC0: Float64LE,  # Declination at epoch 1950.0 (radians)
  ISP:   Bytes(2),   # Spectral type and magnitude code
  MAG:   Int16LE,    # Visual magnitude * 100
  XRPM:  Float32LE,  # Proper motion in RA (arcsec/century)
  XDPM:  Float32LE   # Proper motion in Dec (arcsec/century)
}

header: Bytes(28)
stars: StarEntry[]
```

---

## Lexical Rules

Understanding SDDL's lexical structure helps you write correct specifications.

### Identifiers

Identifiers name fields, records, variables, and parameters.

**Rules:**

- Start with a letter (`a-z`, `A-Z`) or underscore (`_`)
- Contain letters, digits, and underscores
- Are case-sensitive

**Valid identifiers:**
```sddl
count
Count
data_size
_temp
value123
RGB_Color
```

**Invalid identifiers:**
```sddl
123count      # Can't start with digit
data-size     # Can't contain hyphen
my.field      # Can't contain dot
```

**Reserved words:** SDDL has reserved keywords that cannot be used as identifiers:
- `Record`, `Union`, `enum`
- `when`, `then`, `case`, `default`
- `var`, `expect`
- `align`, `pad_to`, `pad_align`
- `scan`, `soa`
- Type names: `Int32LE`, `Float64BE`, etc.

### Statement Termination

At the top level, statements are newline-terminated:

```sddl
header: Header      # Newline terminates this statement
count: Int32LE      # Newline terminates this statement
data: Int32LE[count]  # Newline terminates this statement
```

You cannot put multiple statements on one line at the top level.

### Block Structure

Inside blocks (`{}`, `()`, `[]`), items are comma-separated:

```sddl
Record Point() = {
  x: Float32LE,    # Comma separates from next field
  y: Float32LE,    # Comma separates from next field
  z: Float32LE     # Last field, comma optional
}
```

Trailing commas are allowed:

```sddl
Record Point() = {
  x: Float32LE,
  y: Float32LE,
  z: Float32LE,   # Trailing comma is OK
}
```

This makes it easier to add fields without modifying the previous line.

### Arrays

Array subscripts use `[]`:

```sddl
values: Int32LE[100]           # Fixed size
values: Int32LE[count]         # Parameter size
matrix: Float32LE[rows][cols]  # Multi-dimensional
```

### Whitespace

Whitespace (spaces, tabs, newlines) is generally insignificant except:
- Newlines terminate top-level statements
- Indentation is ignored (not significant like Python)

These are equivalent:

```sddl
Record Point() = {
  x: Float32LE,
  y: Float32LE
}
```

```sddl
Record Point()={x:Float32LE,y:Float32LE}
```

But the first is much more readable. Use whitespace for clarity.

### Comments

Comments are not syntactically significant—they're ignored by the parser. You can place them anywhere:

```sddl
# Header comment
Record Data() = {  # Inline comment
  # Field comment
  value: Int32LE  # Another inline comment
}  # End comment
```

---

## Putting It Together

Let's apply these core concepts to a complete example:

```sddl
# Image file format specification
# Version 1.0

# ----- Types -----

Record Header() = {
  magic: Bytes(4),      # Must be "IMGF"
  version: UInt16LE,    # Format version (currently 1)
  width: UInt32LE,      # Image width in pixels
  height: UInt32LE,     # Image height in pixels
  channels: UInt8,      # Number of color channels (1, 3, or 4)
  bits_per_channel: UInt8  # Bits per channel (8 or 16)
}

Record Pixel8(num_channels) = {
  data: UInt8[num_channels]
}

Record Pixel16(num_channels) = {
  data: UInt16LE[num_channels]
}

# ----- Validation -----

header: Header

expect header.magic == "IMGF"
expect header.version == 1
expect header.channels >= 1 and header.channels <= 4
expect header.bits_per_channel == 8 or header.bits_per_channel == 16

# ----- Variables -----

var num_pixels = header.width * header.height
var num_channels = header.channels
var bits_per_channel = header.bits_per_channel

# ----- Data -----

when bits_per_channel == 8 then pixels_8: Pixel8(num_channels)[num_pixels]
when bits_per_channel == 16 then pixels_16: Pixel16(num_channels)[num_pixels]
```

This example demonstrates:
- Clear type definitions with explicit endianness
- Parameterized records
- Validation with `expect`
- Variables for computed values
- Conditional fields based on format characteristics
- Comments documenting the format

---

## Summary

This chapter defined SDDL's building blocks: primitive and byte types with explicit endianness, records (with parameters and nesting), validation constructs (`expect`/`where`), and the lexical rules that make SDDL readable. With these pieces you can describe fixed structures and enforce invariants before moving on to layout determinism and advanced constructs.

---

## Where to Go Next

- **[Understanding Instant-Parse](instant-parse.md)** to see how layout determinism affects parsing.
- **[Arrays and Collections](arrays-collections.md)** for handling repeated data once you know the basics.
- **[Alignment and Padding](alignment-padding.md)** when you need explicit layout control.
