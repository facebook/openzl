# Getting Started

*Chapter 2 - Learning SDDL through examples*

This chapter teaches you SDDL by writing actual format descriptions, starting from the simplest possible file and building up complexity step by step. By the end, you'll understand how to describe common binary formats and use them with OpenZL.

---

## Your First SDDL File

Let's start with the absolute simplest binary format: a file containing a single 32-bit integer.

**File: `simple.sddl`**
```sddl
value: Int32LE
```

That's it. One line. This describes a binary file containing a single 32-bit little-endian signed integer.

### Anatomy of This Specification

- **`value`** - The field name (you can choose any valid identifier)
- **`:`** - Separates the field name from its type
- **`Int32LE`** - A 32-bit signed integer, little-endian byte order

When this specification is compiled to bytecode and used with OpenZL, the interpreter will:
1. Read 4 bytes from the input
2. Interpret them as a little-endian 32-bit signed integer
3. Route that data to the appropriate compression stream

### Why Little-Endian?

Notice we wrote `Int32LE`, not just `Int32`. SDDL requires explicit endianness for all multi-byte types. This prevents a common source of bugs in binary format handling. You must always choose:

- `Int32LE` or `Int32BE`
- `Float64LE` or `Float64BE`
- And so on for all multi-byte types

There is no global endianness setting. Every field's byte order is explicit and visible.

**See also:** The complete list of primitive types and their endianness requirements is available in the [Language Elements Overview](core-concepts.md#language-elements-overview).

---

## Adding More Fields

Let's describe a slightly more complex format: a simple file header.

**File: `header.sddl`**
```sddl
magic: Bytes(4)
version: Int16LE
flags: Int16LE
data_size: Int32LE
```

This describes a file that begins with:
- 4 bytes of "magic" identifier (often used to verify file type)
- A 2-byte version number (little-endian)
- A 2-byte flags field (little-endian)
- A 4-byte data size field (little-endian)

Total: 12 bytes.

### Field Order Matters

Fields are parsed in the order they're written. The first field (`magic`) corresponds to bytes 0-3, the second field (`version`) corresponds to bytes 4-5, and so on.

### Comments

You can add comments to explain what each field means:

```sddl
magic: Bytes(4)      # File type identifier, typically "MYFT"
version: Int16LE     # Format version, currently 1
flags: Int16LE       # Bit flags for optional features
data_size: Int32LE   # Size of data section in bytes
```

Comments start with `#` and continue to the end of the line.

---

## Defining Records

When a format has multiple occurrences of the same structure, define it as a `Record`:

**File: `with_record.sddl`**
```sddl
Record Point() = {
  x: Float32LE,
  y: Float32LE,
  z: Float32LE
}

origin: Point
center: Point
```

This describes a file containing two 3D points: an origin point and a center point. Each point is 12 bytes (3 floats × 4 bytes each), for a total of 24 bytes.

### Why Use Records?

Records let you:
1. **Reuse structures** - Define once, use many times
2. **Name concepts** - `Point` is clearer than "three floats"
3. **Organize complexity** - Break large formats into manageable pieces

### Record Syntax

```sddl
Record Name() = {
  field1: Type1,
  field2: Type2,
  ...
}
```

- Record names start with a capital letter by convention
- Fields inside records are comma-separated
- Trailing commas are allowed

For a complete reference on Records and all other language constructs, see the [Language Elements Overview](core-concepts.md#language-elements-overview).

---

## Arrays

Most binary formats contain arrays of data. SDDL supports both fixed-size and auto-sized arrays.

### Fixed-Size Arrays

**File: `fixed_array.sddl`**
```sddl
Record Point() = {
  x: Float32LE,
  y: Float32LE,
  z: Float32LE
}

count: Int32LE
points: Point[100]  # Always exactly 100 points
```

The `[100]` syntax creates an array of exactly 100 `Point` structures.

### Dynamic-Size Arrays Using Parameters

Parameters let you specify array sizes that depend on values read from the file:

```sddl
Record Point(dimensions) = {
  coords: Float32LE[dimensions],
}
Record Polygon(dimensions, vertex_count) = {
  points: Point(dimensions)[vertex_count],
}

triangle_count: UInt32LE
triangles : Polygon(3, 3)[triangle_count]
```

### Auto-Sized Arrays

When you want to read "everything remaining in the file," use an array without a size:

```sddl
Record Point() = {
  x: Float32LE,
  y: Float32LE,
  z: Float32LE
}

points: Point[]  # Read points until end of file
```

This repeats the `Point` structure until there's no more data.

> **Learn more:** Arrays are one of SDDL's core language elements. For complete array syntax and capabilities, see [Language Elements Overview](core-concepts.md#language-elements-overview) and [Arrays and Collections](arrays-collections.md).

---

## SAO Example Revisited

Now that we understand records and arrays, let's revisit the SAO example from Chapter 1:

```sddl
# Star catalog entry (28 bytes)
Record StarEntry() = {
  SRA0:  Float64LE,    # Right Ascension (radians)
  SDEC0: Float64LE,    # Declination (radians)
  ISP:   Bytes(2),     # Spectral type
  MAG:   Int16LE,      # Magnitude
  XRPM:  Float32LE,    # R.A. proper motion
  XDPM:  Float32LE     # Dec. proper motion
}

# File structure
header: Bytes(28)
stars: StarEntry[]
```

Breaking this down:

1. **Record Definition**: `StarEntry` defines a 28-byte structure for one star
   - Two 8-byte doubles (SRA0, SDEC0): 16 bytes
   - One 2-byte blob (ISP): 2 bytes
   - One 2-byte integer (MAG): 2 bytes
   - Two 4-byte floats (XRPM, XDPM): 8 bytes
   - Total: 28 bytes

2. **File Structure**: The actual file layout
   - Skip the first 28 bytes (header)
   - Read `StarEntry` structures until end of file

This is a complete, working SDDL specification for the Silesia SAO format.

---

## Referencing Fields

When you parse a field, SDDL creates a binding between the field name and its value. You can reference that field in later expressions using dot notation:

```sddl
Record Header() = {
  magic: Bytes(4),
  count: Int32LE
}

header: Header
items: Item[header.count]  # Reference the count field
```

After parsing `header`, you can use `header.count`, `header.magic`, etc. in array sizes, parameters, conditionals, and validation statements. For nested records, use multiple dots: `header.meta.timestamp`.

> **Note:** Only integer field types (up to signed 64-bit) can be used in expressions. Floating-point types and UInt64LE/UInt64BE are not supported in expressions, though you can still parse them as fields.

> **Learn more:** For complete details on field references, type restrictions, scope rules, and performance implications, see [Variables and Expressions - Referencing Fields](variables-expressions.md#referencing-fields).

---

## Variables

Sometimes you need to extract a value from the data and use it later. That's what `var` statements do:

```sddl
header_magic: Bytes(4)
header_count: Int32LE

var num_items = header_count  # Extract the count value

Record Item() = {
  id: Int32LE,
  value: Float32LE
}

items: Item[num_items]  # Use the extracted value
```

Variables are:

- **Immutable** - Once set, they can't be changed
- **Immediate** - They're evaluated as soon as their dependencies are available
- **Local to their scope** - Variables at the top level are available to all subsequent fields

Variables are covered in depth in [Language Elements Overview](core-concepts.md#language-elements-overview) and [Variables and Expressions](variables-expressions.md).

---

## Conditional Fields

Sometimes fields are optional or depend on flags. Use `when` conditions:

```sddl
Record Packet(has_timestamp, has_checksum) = {
  id: Int32LE,

  when has_timestamp { timestamp: Int64LE },

  payload: Bytes(100),

  when has_checksum { checksum: Int32LE }
}

flags: Int16LE

var include_time = (flags & 0x01) != 0
var include_crc = (flags & 0x02) != 0

packet: Packet(include_time, include_crc)
```

The `when` clause makes a field conditional:
- If the condition is true, the field is parsed
- If the condition is false, the field is skipped

For more on conditional fields and other control flow elements, see the [Language Elements Overview](core-concepts.md#language-elements-overview).

---

## Common Patterns

### Pattern 1: Length-Prefixed Data

Many formats store a length followed by that many bytes:

```sddl
Record Message(size) = {
  data: Bytes(size)
}

message_length: Int32LE
message: Message(message_length)
```

### Pattern 2: Count-Prefixed Array

Store the number of items, then the items themselves:

```sddl
Record Item() = {
  id: Int32LE,
  value: Float32LE
}

count: Int32LE
items: Item[count]
```

### Pattern 3: Fixed Header + Variable Data

A fixed-size header followed by data sections:

```sddl
Record Header() = {
  magic: Bytes(4),
  version: Int16LE,
  num_sections: Int16LE
}

Record Section() = {
  type: Int16LE,
  size: Int16LE,
  data: Bytes(size)
}

header: Header
sections: Section[header.num_sections]
```

### Pattern 4: Flags and Optional Fields

Bit flags controlling optional features:

```sddl
Record Data(flags) = {
  base: Int32LE,
  when (flags & 0x01) != 0 { extra1: Int32LE },
  when (flags & 0x02) != 0 { extra2: Int32LE },
  when (flags & 0x04) != 0 { extra3: Int32LE }
}

flags_field: Int16LE
data: Data(flags_field)
```

These patterns demonstrate SDDL's core language elements in action. For a systematic reference to all available constructs, see the [Language Elements Overview](core-concepts.md#language-elements-overview).

---

## The Conceptual Workflow

Now that you understand how to write SDDL specifications, here's how they fit into the OpenZL workflow:

### Step 1: Write Your SDDL File

Create a `.sddl` file describing your format:

```sddl
# myformat.sddl
Record Header() = {
  magic: Bytes(4),
  count: Int32LE
}

Record DataPoint() = {
  value: Float64LE
}

header: Header
data: DataPoint[header.count]
```

### Step 2: Compile to Bytecode

The SDDL compiler (part of OpenZL's tooling) compiles your specification into bytecode:

```
myformat.sddl  →  [SDDL Compiler]  →  myformat.bytecode
```

The bytecode is a compact representation of the parsing instructions that the SDDL interpreter can execute efficiently.

### Step 3: Use During Training

OpenZL's training phase uses the bytecode to understand your data structure and find optimal compression strategies:

```
myformat.bytecode + training_data  →  [Training]  →  trained_model
```

### Step 4: Use During Compression

The bytecode is used during actual compression to parse the data and route it to the appropriate compression streams:

```
myformat.bytecode + input_file  →  [Compression]  →  compressed_output
```

### The Key Insight

The bytecode acts as a reusable parsing program. You write the SDDL specification once, and it's used both during training and compression. When your format changes, you update the SDDL file, recompile, and retrain—without writing any parsing code.

---

## Instant-Parse: A First Look

You may have noticed in Chapter 1 that SDDL distinguishes between "instant-parse" and "requires-scan" formats. Let's see what this means with examples:

### Instant-Parse Example

```sddl
Record Header(data_size) = {
  magic: Bytes(4),
  payload: Bytes(data_size)  # Size is a parameter
}

size: Int32LE
header: Header(size)
```

This is **instant-parse** because the size of `payload` comes from a parameter (`data_size`), which is known before parsing the `Header`. The layout is completely determined by the parameter.

### Requires-Scan Example

```sddl
Record Header() = {
  magic: Bytes(4),
  size: Int32LE,
  payload: Bytes(size)  # Size depends on a local field
}

header: Header
```

This **requires-scan** because the size of `payload` depends on the `size` field within the same record. You must scan sequentially to know how large `payload` is.

### Why Does This Matter?

Instant-parse formats can be parsed much faster. We'll explore this in detail in Chapter 4. For now, just be aware that:

- Parameters are instant-parse safe
- References to local fields require scanning
- SDDL will tell you which category your format falls into

---

## Common Mistakes and How to Avoid Them

### Mistake 1: Forgetting Endianness

!!! danger "Wrong"
    ```sddl
    value: Int32  # ERROR: No endianness specified
    ```

**Right:**
```sddl
value: Int32LE  # Explicitly little-endian
```

SDDL will reject any multi-byte type without explicit endianness.

### Mistake 2: Field Name Conflicts

!!! danger "Wrong"
    ```sddl
    Record Data() = {
      value: Int32LE,
      value: Float32LE  # ERROR: Duplicate field name
    }
    ```

**Right:**
```sddl
Record Data() = {
  int_value: Int32LE,
  float_value: Float32LE
}
```

Each field name must be unique within a record (except `_`, which can repeat for throwaway fields).

### Mistake 3: Using Out-of-Scope Fields as Parameters

!!! danger "Wrong"
    ```sddl
    size: Int32LE

    Record Container() = {
      data: SubRecord(size)  # ERROR: Can't use out-of-scope field as parameter
    }

    container: Container
    ```

**Right - Pass as parameter:**
```sddl
Record Container(size) = {
  data: SubRecord(size)
}

size: Int32LE
container: Container(size)
```

Parameters must come from the record's own scope or be passed in from above. You cannot reference fields or variables defined outside the record unless they're explicitly passed as parameters.

**Note:** Using a local field within the same record IS allowed:
```sddl
Record Container() = {
  size: Int32LE,
  data: SubRecord(size)  # OK: size is a local field
}
```

This makes the record require scanning, but it's perfectly valid.

### Mistake 4: Missing Comma in Records

!!! danger "Wrong"
    ```sddl
    Record Point() = {
      x: Float32LE
      y: Float32LE  # ERROR: Missing comma after x
      z: Float32LE
    }
    ```

**Right:**
```sddl
Record Point() = {
  x: Float32LE,
  y: Float32LE,
  z: Float32LE
}
```

Fields within records are comma-separated. Trailing commas are allowed.

---

## Building Up Complexity

Let's practice by building a complete, realistic format step by step.

### Version 1: Just the Header

```sddl
Record Header() = {
  magic: Bytes(4),      # Should be "MYFT"
  version: Int16LE,     # Format version
  num_records: Int32LE  # Number of data records
}

header: Header
```

### Version 2: Add Data Records

```sddl
Record Header() = {
  magic: Bytes(4),
  version: Int16LE,
  num_records: Int32LE
}

Record DataRecord() = {
  id: Int32LE,
  timestamp: Int64LE,
  value: Float64LE
}

header: Header
records: DataRecord[header.num_records]
```

### Version 3: Add Optional Metadata

```sddl
Record Header() = {
  magic: Bytes(4),
  version: Int16LE,
  flags: Int16LE,       # Bit 0: has metadata
  num_records: Int32LE
}

Record DataRecord() = {
  id: Int32LE,
  timestamp: Int64LE,
  value: Float64LE
}

Record Metadata() = {
  author: Bytes(32),
  created: Int64LE,
  description: Bytes(128)
}

header: Header

var has_metadata = (header.flags & 0x01) != 0
when has_metadata { metadata: Metadata }

records: DataRecord[header.num_records]
```

Each version adds complexity while remaining readable and maintainable.

---

## Next Steps

You now understand the basics of SDDL:

- How to describe simple formats
- Records and arrays
- Parameters and variables
- Conditional fields
- Common patterns

The next chapters will deepen your understanding:

- **[Core Concepts](core-concepts.md)** - The fundamental building blocks in detail, including the comprehensive [Language Elements Overview](core-concepts.md#language-elements-overview)
- **[Arrays and Collections](arrays-collections.md)** - Advanced array techniques
- **[Understanding Instant-Parse](instant-parse.md)** - Performance implications
- **[Variables and Expressions](variables-expressions.md)** - Computing derived values

**💡 Need Help?** Consider using **[SDDL for LLM](sddl-for-llm.md)** with your AI assistant (ChatGPT, Claude, etc.) to get help writing SDDL specifications, validating syntax, or learning through examples.

---

## A Note on Tooling

The SDDL compiler and its command-line interface are evolving. Specific commands and options will change as the tooling matures. This chapter focused on the SDDL language itself, which is stable in its core design. For current information about compiler usage and OpenZL integration, refer to the OpenZL documentation and release notes.

The language concepts you've learned here—records, arrays, parameters, variables—are the foundation that remains constant regardless of how the tooling evolves.

---

## Summary

In this chapter, you learned:

- SDDL specifications describe binary formats through progressive examples
- Endianness must always be explicit
- Records organize reusable structures
- Arrays can be fixed-size, parameter-sized, or auto-sized
- Variables extract values for later use
- Conditional fields handle optional data
- The workflow: write SDDL → compile to bytecode → use with OpenZL
- Common patterns for typical binary format scenarios

You're now ready to explore SDDL's features in greater depth in the following chapters.
