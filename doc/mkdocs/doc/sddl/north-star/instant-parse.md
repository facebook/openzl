# Understanding Instant-Parse

*Chapter 4 - Understanding layout determinism*

The instant-parse model is SDDL's most distinctive feature. It distinguishes between formats whose layout can be computed from parameters alone and those that require sequential scanning. This chapter explains what instant-parse means, when it matters, and how to work with it.

For an overview of SDDL's language elements and how instant-parse applies across them, see the [Language Elements Overview](core-concepts.md#language-elements-overview).

---

## What Is Instant-Parse?

An **instant-parse** type is one where all field offsets, sizes, and layout can be computed from parameters and constants alone, without examining the data itself.

### The Core Distinction

**Instant-Parse:**
```sddl
Record Header(payload_size) = {
  magic: Bytes(4),
  version: Int16LE,
  payload: Bytes(payload_size)  # Size is a parameter - known upfront
}
```

The layout of this record is fully determined by the `payload_size` parameter. Given that parameter, you can compute:

- Magic starts at offset 0
- Version starts at offset 4
- Payload starts at offset 6
- Total record size is 6 + payload_size

No data needs to be read to know these offsets.

**Requires-Scan:**
```sddl
Record Message() = {
  magic: Bytes(4),
  size: Int16LE,
  payload: Bytes(size)  # Size depends on parsed field - must scan
}
```

Here, you cannot know the payload offset or total record size until you read and parse the `size` field. The layout depends on data values, not just parameters.

### Why This Matters

Instant-parse status affects what you can do with the format:

1. **Parallel processing:** Instant-parse arrays can be dispatched instantly, allowing parallel processing
2. **Zero-copy access:** Fields in instant-parse records can be accessed directly without parsing
3. **Predictability:** Layout behavior is explicit and enforceable at compile time

The payoff is especially dramatic for arrays. A single record that requires a scan only delays parsing by a few bytes. The same record, when repeated thousands of times, forces the parser to walk the file sequentially element by element. Instant-parse arrays, by contrast, let the runtime jump to element `i` immediately, fan out work across CPU cores, and keep compression streams fed without waiting. As you read through the next chapter, keep in mind that the instant-parse property effectively determines whether your arrays behave like random-access tables or sequential streams.

These capabilities make instant-parse formats faster to work with, but the key insight is that instant-parse is a **property of the format description**, not a performance optimization directive.

---

## When a Type Requires Scanning

A construct requires scanning when any of these conditions hold:

### 1. Dependencies on Parsed Fields

The most common case: a field or array size depends on another field referenced within the same scope.

```sddl
Record Container() = {
  count: UInt32LE,
  items: Item[count]  # Depends on local field: requires scan
}
```

The `items` array size isn't known until `count` is parsed.

### 2. Delimiter-Based Parsing

Any delimiter-based construct requires scanning:

```sddl
text: Bytes until "\r\n\r\n"      # Must scan to find delimiter
name: Bytes until 0x00            # Must scan to find null terminator
```

You cannot know the field size without scanning for the delimiter.

### 3. State-Dependent Functions

Functions like `current_position()` depend on parsing state:

```sddl
Record Dynamic() = {
  field1: Bytes(10),
  var offset = current_position(),  # Depends on parse state
  field2: Bytes(offset)
}
```

This makes the record require scanning.

### 5. Transitivity

Instant-parse status is transitive. A construct is instant-parse only if all its components are instant-parse:

```sddl
Record Inner() = {
  size: UInt16LE,
  data: Bytes(size)  # Requires scan
}

Record Outer() = {
  inner: Inner  # Also requires scan because Inner does
}
```

If any component requires scanning, the entire containing structure requires scanning.

---

## The `@instant_parse` Annotation

You can enforce instant-parse status with the `@instant_parse` annotation.

### Basic Usage

```sddl
Record Header(has_checksum) = {
  magic: Bytes(4),
  version: UInt16LE,
  when (flags & 0x01) != 0 { checksum: UInt32LE }  # Requires scan
} @instant_parse
```

If this record were modified to break instant-parse guarantees, the compiler would reject it:

!!! danger "Compiler error"
    ```sddl
    Record Header() = {
      magic: Bytes(4),
      version: Int16LE,
      size: Int16LE,
      data: Bytes(size)  # ERROR: depends on local field
    } @instant_parse
    ```

    **Compiler output:**
    ```
    ERROR: Record `Header` is not instant-parse
      Field `data` depends on local field `size` (line 5)
    ```

### Why Use `@instant_parse`?

The annotation serves two purposes:

1. **Documentation:** Makes the instant-parse requirement explicit
2. **Enforcement:** Prevents accidental changes that break instant-parse

Use it when instant-parse is important to your format's design and you want the compiler to verify it.

### Annotating Fields

You can also annotate individual fields:

```sddl
Record Container(count) = {
  header: Bytes(16),
  items: Item[count] @instant_parse  # Enforce that items array is instant-parse
}
```

This verifies that `Item` is instant-parse and `count` is a parameter.

---

## Instant-Parse in Different Contexts

### Records

A record is instant-parse when all its fields and their sizes depend only on parameters:

```sddl
Record Packet(payload_size) = {
  header: Bytes(12),
  payload: Bytes(payload_size)
} @instant_parse
```

### Arrays

An array is instant-parse when:
- Its element type is instant-parse
- Its size depends only on parameters or constants

```sddl
Record Grid(width, height) = {
  cells: Cell[height][width]  # Instant-parse if Cell is instant-parse
} @instant_parse
```

### Conditional Fields

Conditional fields using parameters are instant-parse:

```sddl
Record Packet(has_timestamp, has_checksum) = {
  id: Int32LE,
  payload: Bytes(100),
  when has_timestamp { timestamp: Int64LE },
  when has_checksum { checksum: UInt32LE }
} @instant_parse
```

Conditions referencing local fields require scanning:

```sddl
Record Packet() = {
  id: Int32LE,
  flags: UInt8,
  when (flags & 0x01) != 0 { extra: Int32LE }  # Requires scan
}
```

### Unions

A union is instant-parse when its selector is a parameter and all arms are instant-parse:

```sddl
Union Payload(type, size) = {
  case 1: Image(size),
  case 2: Audio(size),
  default: Raw(size)
}

type: UInt8
size: UInt32LE
payload: Payload(type, size) @instant_parse
```

---

## Practical Examples

### Example 1: Image Header (Instant-Parse)

```sddl
Record ImageHeader() = {
  magic: Bytes(4),      # "IMGF"
  width: UInt32LE,
  height: UInt32LE,
  channels: UInt8,
  bits_per_channel: UInt8,
  _: Bytes(2)           # Padding
} @instant_parse

expect header.magic == "IMGF"
expect header.channels <= 4
expect header.bits_per_channel == 8 or header.bits_per_channel == 16
```

The header is fixed-size and instant-parse. All fields have known positions.

### Example 2: TLV Format (Requires Scan)

```sddl
# Type-Length-Value format
Record TLV() = {
  type: UInt8,
  length: UInt16LE,
  value: Bytes(length)  # Depends on local field: requires scan
}

entries: scan TLV[]
```

This is the standard TLV pattern. It requires scanning because each value's size depends on its length field.

---

## Understanding Compiler Diagnostics

When the compiler reports an instant-parse violation, it explains the dependency chain.

### Example Diagnostic

```sddl
Record Container() = {
  count: UInt32LE,
  items: Item[count]
} @instant_parse
```

!!! danger "Compiler error"
    **Compiler output:**
    ```
    ERROR: Record `Container` is not instant-parse
      Field `items` has size dependent on local field `count` (line 3)

      Dependency chain:
        items[size] → count → local field

      Suggestion: Pass `count` as a parameter instead
    ```

The diagnostic shows:
- What field violates instant-parse
- Why it violates instant-parse
- The dependency chain
- A suggested fix

### Tracing Dependencies

Complex violations may have long dependency chains:

```sddl
Record A() = {
  size: UInt16LE,
  b: B(size)
} @instant_parse

Record B(n) = {
  c: C[n]
}

Record C() = {
  value: Int32LE
}
```

!!! danger "Compiler error"
    **Compiler output:**
    ```
    ERROR: Record `A` is not instant-parse
      Field `b` depends on local field `size` (line 3)

      Dependency chain:
        b → B(size) → size → local field

      Even though B and C are instant-parse, passing a local field
      to B makes the result non-instant-parse.
    ```

The compiler traces through the entire chain to explain the violation.

---

## Common Patterns

### Count-Prefixed Array (Requires Scan)

```sddl
Record Data() = {
  count: UInt32LE,
  values: Int32LE[count]  # Not instant-parse: depends on local field
}
```

### Count as Parameter (Instant-Parse)

```sddl
Record Data(item_count) = {
  values: Int32LE[item_count]  # Instant-parse: depends on parameter
}

count: UInt32LE
data: Data(count)
```

### Variable-Length Field (Requires Scan)

```sddl
Record Entry() = {
  name_len: UInt8,
  name: Bytes(name_len)  # Requires scan
}

entries: scan Entry[]
```

### Fixed-Length Field (Instant-Parse)

```sddl
Record Entry() = {
  name: Bytes(32)  # Always 32 bytes, instant-parse
}
```

### Conditional on Local Field (Requires Scan)

```sddl
Record Packet() = {
  flags: UInt8,
  data: Bytes(100),
  when (flags & 0x01) != 0 then checksum: UInt32LE  # Requires scan
}
```

### Conditional on Parameter (Instant-Parse)

```sddl
Record Packet(has_checksum) = {
  id: Int32LE,
  when has_checksum { checksum: UInt32LE }  # Instant-parse
} @instant_parse

flags: UInt8
var has_crc = (flags & 0x01) != 0
packet: Packet(has_crc)
```

---

## Summary

Instant-parse is a fundamental concept in SDDL:

**Definition:**

- Instant-parse types have layout computable from parameters alone
- Requires-scan types need sequential parsing to determine layout

**When Scanning Is Required:**

- Dependencies on parsed fields
- Delimiter-based constructs
- State-dependent functions
- Transitivity: any component requiring scan makes the whole require scan

**The `@instant_parse` Annotation:**

- Documents instant-parse requirements
- Enforces compile-time verification
- Provides clear diagnostics when violated

**Key Insight:**
Instant-parse is a property of the format description, not a performance directive. It describes whether the layout is statically computable from parameters alone. SDDL describes existing data layouts; you cannot "transform" a requires-scan format into instant-parse—you can only describe the format as it actually exists.

---

## Where to Go Next

- **[Arrays and Collections](arrays-collections.md)** to see how instant-parse affects repeated data.
- **[Alignment and Padding](alignment-padding.md)** if you need precise control over layout.
- **[Conditional & Variant Data](conditional-variant.md)** for optional fields and unions.
