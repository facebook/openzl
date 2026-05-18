# Core Concepts

This page explains the SDDL language features supported by the SDDL2 compiler today. For a compact lookup table of all syntax, see the [Quick Reference](reference.md).

## Primitive Types

SDDL provides integer, float, and byte-sequence types. Every multi-byte type requires an explicit endianness suffix: `LE` for little-endian, `BE` for big-endian. Single-byte types (`Byte`, `Int8`, `UInt8`) don't need one.

For the complete list of all supported types and their sizes, see the [type tables in the Quick Reference](reference.md#types).

### Integers

Integer fields produce values that can be used in expressions — arithmetic, comparisons, array lengths, and conditions:

```sddl
count: UInt32LE
offset = count * 4
expect offset <= 1024
data: Byte[offset]
```

### Floats

Float types (`Float32LE`, `Float64LE`, `BFloat16BE`, etc.) describe how the engine should segment binary data, but their values **cannot be used in expressions**. You cannot do arithmetic or comparisons with float fields — they are type descriptors only.

```sddl
# OK: segmenting data as floats for better compression
coordinates: Float64LE[100]

# NOT allowed: using a float value in an expression
# x: Float32LE
# expect x > 0.0   # Error — float values can't be used in expressions
```

### Byte Sequences

`Bytes(n)` consumes exactly `n` bytes as raw (untyped) data. The argument can be a literal or a variable:

```sddl
magic: Bytes(4)
name: Bytes(name_length)
```

## Records

Records group related fields into a named structure. They are the primary way to describe the layout of binary data.

### Basic Records

```sddl
record Header() {
  magic: UInt32LE,
  version: UInt16LE,
  flags: UInt16LE
}
```

- Declared with `record Name() { ... }`
- Fields are comma-separated `name: Type` pairs
- Empty parentheses `()` are required even with no parameters

### Parameterized Records

Records can accept parameters that control their structure. This lets you define a single record that adapts to different variants of a format:

```sddl
record DataBlock(element_count) {
  checksum: UInt32LE,
  data: UInt16LE[element_count]
}

header: Header
block: DataBlock(header.count)
```

Parameters can be used in array lengths, `when` conditions, and expressions within the record. When instantiating the record, you pass values (literals, variables, or field accesses) as arguments.

### Nested Records

Records can contain fields of other record types. Use dot notation to access nested fields:

```sddl
record Point() {
  x: Int16LE,
  y: Int16LE
}

record Sprite() {
  id: UInt32LE,
  position: Point
}

sprite: Sprite
expect sprite.position.x >= 0
```

Chained access works to any depth: `outer.middle.inner.field`.

### Anonymous (Inline) Records

When you need a one-off record structure without defining a named type, use an anonymous record:

```sddl
: record() {
  id: Int32LE,
  val: Int32LE
}
```

This is useful for simple groupings where a named record would add unnecessary boilerplate.

## Arrays

### Fixed-Size Arrays

Consume a type a specific number of times:

```sddl
values: UInt32LE[100]
matrix: Float64LE[rows * cols]
```

The length can be a literal, a variable, or an arithmetic expression.

### Auto-Sized Arrays

Consume a type until the remaining input is exhausted:

```sddl
entries: StarEntry[]
```

Auto-sized arrays must appear at the end of the description, since they consume all remaining bytes.

## Variables and Expressions

### Variable Assignment

Variables are created in two ways:

**From consumption** — the `:` operator reads data and stores the result:
```sddl
header: Header
```

**From expressions** — the `=` operator computes a value:
```sddl
total_size = header.width * header.height
num_rows = header.file_size / sizeof(Row)
```

Variables are **single-assignment** — once set, they cannot be reassigned.

### Member Access

Access fields of consumed records with dot notation:

```sddl
header: Header
expect header.version == 1
data: Byte[header.size]
```

Chained access works for nested records: `outer.inner.field`.

### Operators

SDDL supports arithmetic operators (`+`, `-`, `*`, `/`, `%`), unary negation (`-expr`), comparison operators (`==`, `!=`, `>`, `>=`, `<`, `<=`), and logical operators (`&&`, `||`, `!`). See the [operator tables in the Quick Reference](reference.md#operators) for the complete list.

Use parentheses to control evaluation order:

```sddl
row_bytes = 4 * ((width + 3) / 4)
```

## Built-in Functions

SDDL provides three built-in functions:

**`sizeof`** returns the size in bytes of a type. Only works on types with statically known sizes:

```sddl
expect header.entry_size == sizeof(Row)
expect sizeof(StarEntry(STNUM, MPROP, NMAG)) == header.NBENT
```

**`abs()`** returns the absolute value of an integer expression:

```sddl
count = abs(header.signed_count)
magnitudes: Int16LE[abs(NMAG)]
```

**`between(l, x, h)`** returns `1` if `l <= x <= h` and `0` otherwise. Useful inside `expect` for range checks:

```sddl
expect between(1, header.version, 3)
expect between(0, count, max_count)
```

## Comments

Single-line comments start with `#`:

```sddl
# This is a comment
magic: UInt32LE  # inline comment
```
