# Quick Reference

A concise reference for all syntax currently supported by the SDDL2 compiler.

## Types

### Integer Types

| Type | Size | Signed | Endianness |
|------|------|--------|------------|
| `Byte` | 1 | No | N/A |
| `Int8` / `UInt8` | 1 | Yes / No | N/A |
| `Int16LE` / `Int16BE` | 2 | Yes | Little / Big |
| `UInt16LE` / `UInt16BE` | 2 | No | Little / Big |
| `Int32LE` / `Int32BE` | 4 | Yes | Little / Big |
| `UInt32LE` / `UInt32BE` | 4 | No | Little / Big |
| `Int64LE` / `Int64BE` | 8 | Yes | Little / Big |
| `UInt64LE` / `UInt64BE` | 8 | No | Little / Big |

### Float Types (type descriptors only — cannot be used in expressions)

| Type | Size | Endianness |
|------|------|------------|
| `Float16LE` / `Float16BE` | 2 | Little / Big |
| `Float32LE` / `Float32BE` | 4 | Little / Big |
| `Float64LE` / `Float64BE` | 8 | Little / Big |
| `BFloat16LE` / `BFloat16BE` | 2 | Little / Big |

### Byte Sequences

| Syntax | Description |
|--------|-------------|
| `Bytes(n)` | Exactly `n` bytes as a serial field |

## Syntax Quick Reference

### Records

```sddl
record Name() { field: Type, ... }         # basic
record Name(PARAM1, PARAM2) { ... }        # parameterized
: record() { field: Type, ... }              # anonymous/inline
```

### Arrays

```sddl
name: Type[length]     # fixed-size array
name: Type[]           # auto-sized (consumes remaining input)
```

### Consumption

```sddl
: Type              # consume without storing
name: Type          # consume and store in variable
```

### Variables

```sddl
name = expression           # assign from expression
name: Type                  # assign from consumption
name.field                  # member access
name.inner.field            # chained member access
```

### Conditional Fields

```sddl
record Name(COND) {
  when COND {
    field1: Type1,
    field2: Type2
  },
  field3: Type3
}
```

### Validation

```sddl
expect condition
```

## Operators

### Arithmetic

| Op | Description |
|----|-------------|
| `+` `-` `*` `/` `%` | Add, subtract, multiply, divide, modulo |
| `-expr` | Unary negation |

### Comparison

| Op | Description |
|----|-------------|
| `==` `!=` | Equal, not equal |
| `>` `>=` `<` `<=` | Relational |

### Logical

| Op | Description |
|----|-------------|
| `&&` | AND |
| `||` | OR |
| `!` | NOT |

## Builtin Functions

| Function | Description |
|----------|-------------|
| `sizeof(Type)` | Size in bytes (static types only) |
| `abs(expr)` | Absolute value |

## Comments

```sddl
# single-line comment
field: Type  # inline comment
```
