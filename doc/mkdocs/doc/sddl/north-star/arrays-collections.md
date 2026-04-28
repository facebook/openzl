# Arrays and Collections

*Chapter 5 - Working with repeated data*

Arrays are fundamental to binary format descriptions. Most real-world formats contain sequences of repeated structures—pixels in images, samples in audio, records in databases. This chapter explores how SDDL handles arrays, from simple fixed-size collections to complex structure-of-arrays layouts.

For a high-level overview of how arrays fit into SDDL's overall syntax, see the [Language Elements Overview](core-concepts.md#language-elements-overview).

Because arrays repeat the same type many times, they amplify whatever instant-parse behavior that type has. An element that takes a small scan penalty once will take it thousands of times inside an array. The previous chapter's instant-parse rules are the lens through which we evaluate array performance. For concrete specs that use these layouts, see the coverage map entries for [fixed arrays](real-formats.md#coverage-arrays-fixed), [parameterized arrays](real-formats.md#coverage-arrays-parameter), and [auto-sized arrays](real-formats.md#coverage-arrays-auto); a future row tracks the pending example for [`scan`-based arrays](real-formats.md#coverage-scan).

---

## Fixed-Size Arrays

A common array form specifies an exact element count.

### Basic Syntax

```sddl
values: Int32LE[100]           # Exactly 100 integers
pixels: UInt8[1920][1080]      # 2D array: 1920 × 1080 pixels
colors: RGB[256]               # 256 RGB color entries
```

The number in brackets specifies how many elements to parse. This can be:

- **A literal constant:** `Int32LE[100]`
- **A parameter:** `Int32LE[count]` where `count` is a record parameter
- **A variable:** `Int32LE[num_items]` where `num_items` was defined with `var`
- **An expression:** `Int32LE[width * height]`

### Multi-Dimensional Arrays

SDDL supports multi-dimensional arrays with multiple bracket pairs:

```sddl
record Image(width, height, channels) {
  pixels: UInt8[height][width][channels]
}
```

Layout is row-major: the rightmost index varies fastest. In the example above, the three channel values for pixel (0,0) come first, followed by channels for pixel (0,1), and so on.

### Arrays of Records

Arrays work with any type, including records:

```sddl
record Point() {
  x: Float32LE,
  y: Float32LE,
  z: Float32LE
}

record PointCloud(num_points) {
  points: Point[num_points]
}

count: UInt32LE
cloud: PointCloud(count)
```

This creates an array of `Point` structures laid out sequentially in memory.

### Parameterized Element Types

Array elements can themselves be parameterized:

```sddl
record Block(size) {
  header: Bytes(4),
  data: Bytes(size - 4)
}

record FileData(block_size, block_count) {
  blocks: Block(block_size)[block_count]
}
```

Each `Block` in the array uses the same `block_size` parameter.

---

## Auto-Sized Arrays

When the array size is not known nor declared in advance, one can use auto-sized arrays.

### Reading Until End of Scope

An array without a size specification repeats until there's no more data:

```sddl
record Entry() {
  id: Int32LE,
  value: Float64LE
}

entries: Entry[]  # Read Entry records until end of file
```

This consumes all remaining data in the current scope, parsing `Entry` structures repeatedly.
Within a scope, only a single array can be auto-sized.

### What Is "Scope"?

Scope depends on context:

- **At the top level:** End of file
- **Inside a record with known size:** End of the record's data

Example with explicit scope:

```sddl
record Container(payload_size) {
  header: Bytes(16),
  entries: Entry[]                # Reads until payload_size is exhausted
} pad_to payload_size
```

If the auto-sized array is not the last member of the scope, all following members in the same scope must be instant-parse, and all elements required to determine their size must be known before the array.

### Partial Elements and Leftover Data

What happens when the remaining data in scope isn't evenly divisible by the element size? The behavior depends on the enclosing scope.

**File Scope (or Non-Padded Record Scope)**

When an auto-sized array appears at file level, **all remaining data must form complete elements**. Leftover bytes that don't constitute a full element cause a parse error.

```sddl
record Point() {
  x: Float32LE,  # 4 bytes
  y: Float32LE,  # 4 bytes
  z: Float32LE   # 4 bytes
}

points: Point[]  # Each Point is 12 bytes
```

If the file contains 100 bytes:
- **96 bytes = 8 complete points**: ✓ Valid
- **100 bytes = 8 complete points + 4 leftover bytes**: ✗ Parse error

The 4 leftover bytes are insufficient to form a complete `Point` structure.

**Padded Record Scope**

When an auto-sized array appears within a record that uses `pad_to` or `pad_align`, leftover bytes are **allowed and treated as padding**.

```sddl
record Container(payload_size) {
  header: Bytes(16),
  entries: Entry[]  # Auto-sized array
} pad_to payload_size
```

If `payload_size` is 100 and `Entry` is 12 bytes:
- The `header` consumes 16 bytes
- Remaining space: 84 bytes
- 84 ÷ 12 = 7 complete entries (84 bytes used)
- **Leftover: 0 bytes** (padding not needed in this case)

If `payload_size` is 104:
- The `header` consumes 16 bytes
- Remaining space: 88 bytes
- 88 ÷ 12 = 7 complete entries (84 bytes used)
- **Leftover: 4 bytes** ✓ Valid (treated as padding)

This allows the `Container` to meet its `pad_to` size requirement while the array consumes only complete elements.


### Combining Fixed and Auto-Sized Dimensions

You can mix fixed and auto-sized dimensions:

```sddl
record Scanline(width) {
  pixels: RGB[width]
}

# Read scanlines until end of data
# Each scanline has a fixed width
image: Scanline(1920)[]
```

The first dimension is auto-sized (unknown number of scanlines), the second is fixed (exactly 1920 pixels per scanline).

### The `scan` Keyword

When array elements require scanning (they're not instant-parse), you must use the `scan` keyword:

```sddl
record VariableEntry() {
  size: UInt16LE,
  data: Bytes(size)  # Size depends on local field: requires scan
}

# Must use 'scan' because VariableEntry requires scanning
entries: scan VariableEntry[]
```

The `scan` keyword makes the scanning requirement explicit. If you forget it, the compiler will remind you with an error.

### When Auto-Sized Arrays Make Sense

Auto-sized arrays are useful when:
- The format doesn't store a count
- You're parsing a stream of unknown length
- The file format is "records until EOF"
- You want to consume all remaining data

---

## Arrays and Instant-Parse

Whether an array is instant-parse depends on its element type and size specification.

### Instant-Parse Arrays

An array is instant-parse when:
- Its element type is instant-parse
- Its size depends only on parameters or constants

```sddl
record Grid(width, height) {
  cells: Cell[height][width]
} @instant_parse
```

If `Cell` is instant-parse and `width`/`height` are parameters, the entire `Grid` is instant-parse.

### Non-Instant-Parse Arrays

Arrays become non-instant-parse when:
- The element type requires scanning
- The size depends on local fields

```sddl
record Data() {
  count: UInt32LE,
  values: Int32LE[count]  # Size depends on local field
}
```

This `Data` record is not instant-parse because `count` is a local field, not a parameter.
Note that `Data.values` itself is an instant-parse array, since `Int32LE` is instant-parse.

---

## Array Layout and Alignment

### Default Layout

Arrays are laid out with elements immediately following each other, with no padding between elements.

```sddl
values: Int32LE[100]  # 400 bytes: 100 × 4 bytes, no padding
```

### Aligned Elements

If the element type has alignment requirements, padding may appear between elements:

```sddl
record Aligned() {
  value: UInt8,
  important: align(8) Int64LE  # Must start at 8-byte boundary
}

records: Aligned[100]
```

Each `Aligned` record may include padding after `value` to ensure `important` starts at an 8-byte boundary. The array will include padding between elements to maintain alignment.

### Record Padding

Records with `pad_align` or `pad_to` affect array layout:

```sddl
record Padded() {
  data: Bytes(10)
} pad_align 16  # Each record is a multiple of 16 bytes

records: Padded[50]  # Each record occupies 16 bytes (10 data + 6 padding)
```

The padding ensures consistent element sizes, which can improve cache performance.

---

## Working with Arrays: Common Patterns

### Pattern 1: Count-Prefixed Array

The most common array pattern: a count field followed by that many elements.

```sddl
record Item() {
  id: Int32LE,
  value: Float32LE
}

count: UInt32LE
items: Item[count]
```

### Pattern 2: Type-Specific Arrays

Different array types based on a flag or version:

```sddl
record Header() {
  version: UInt16LE,
  count: UInt32LE
}

header: Header

when header.version == 1 { data_v1: DataV1[header.count] }
when header.version == 2 { data_v2: DataV2[header.count] }
```

### Pattern 3: Nested Arrays

Arrays of arrays for grid or matrix data:

```sddl
record Row(width) {
  cells: Cell[width]
}

record Grid(width, height) {
  rows: Row(width)[height]
}
```

### Pattern 4: Chunked Data

Split data into fixed-size chunks:

```sddl
record Chunk() {
  data: Bytes(4096)
}

num_chunks: UInt32LE
chunks: Chunk[num_chunks]
```

Each chunk is exactly 4096 bytes, making the data easy to process in blocks.

### Pattern 5: Mixed Fixed and Variable

Combine fixed-size headers with variable-size payloads:

```sddl
record PacketHeader() {
  id: UInt32LE,
  payload_size: UInt16LE,
  flags: UInt16LE
}

record Packet() {
  header: PacketHeader,
  payload: Bytes(header.payload_size)
}

packets: scan Packet[]  # Variable-size packets until end of file
```

---

## Structure-of-Arrays Layout

By default, arrays store elements sequentially: all fields of element 0, then all fields of element 1, and so on. This is called array-of-structures (AOS).

### Array-of-Structures (Default)

```sddl
record Particle() {
  x: Float32LE,
  y: Float32LE,
  z: Float32LE,
  vx: Float32LE,
  vy: Float32LE,
  vz: Float32LE
}

particles: Particle[1000]
```

**Memory layout:**
```
x0 y0 z0 vx0 vy0 vz0 | x1 y1 z1 vx1 vy1 vz1 | x2 y2 z2 vx2 vy2 vz2 | ...
```

Each particle's data is contiguous.

### Structure-of-Arrays with `soa`

Some binary formats store data in structure-of-arrays (SOA) layout.

```sddl
record Particle() {
  x: Float32LE,
  y: Float32LE,
  z: Float32LE,
  vx: Float32LE,
  vy: Float32LE,
  vz: Float32LE
}

particles: soa Particle[1000]
```

**Memory layout:**
```
x0 x1 x2 ... x999 | y0 y1 y2 ... y999 | z0 z1 z2 ... z999 | vx0 vx1 ... | vy0 vy1 ... | vz0 vz1 ...
```

All `x` values are contiguous, then all `y` values, and so on.

**Alternative description:** You could describe this same layout as six separate arrays:

```sddl
x_values: Float32LE[1000],
y_values: Float32LE[1000],
z_values: Float32LE[1000],
vx_values: Float32LE[1000],
vy_values: Float32LE[1000],
vz_values: Float32LE[1000]
```

**Why use `soa` instead:** Using `soa Particle[1000]` is clearer because:

*   It makes explicit that these six arrays represent 1000 particles (semantic relationship)
*   It ensures all arrays have the same count (enforced by the format)
*   It's compatible with auto-sizing: `soa Particle[]` reads until end of scope

### SOA and Nested Records

SOA layout only unwraps the first level of fields. If a field is itself a record,
it remains in array-of-structures (AOS) layout:

```sddl
record Color() {
  r: UInt8,
  g: UInt8,
  b: UInt8
}

record Pixel() {
  position: Int16LE,
  color: Color       # Nested record
}

pixels: soa Pixel[100]
```

**Layout**:

```
pos0 pos1 ... pos99 | (r0 g0 b0) (r1 g1 b1) ... (r99 g99 b99)
                      └────── Color stays AOS ──────┘
```

This creates 2 arrays: one for `position` fields, one for `color` structures (each color structure contains r, g, b in sequence).

### SOA and Array Members

If a record field is itself a fixed-size array, it remains as a contiguous block:

```sddl
record Item() {
  id: UInt32LE,
  values: Float32LE[3]  # Fixed-size array member
}

items: soa Item[100]
```

**Layout:**

```
id0 id1 ... id99 | (v0 v1 v2)₀ (v0 v1 v2)₁ ... (v0 v1 v2)₉₉
```

This creates 2 arrays: one for `id` fields, one for `values` triplets.


### `soa` Requirements

The element type must be structured so that each field's size and layout can be
determined from fields that appear earlier in the record definition. This ensures
the SOA layout can be parsed field-array by field-array.

Simple instant-parse records (all fixed-size fields) always satisfy this requirement:

```sddl
record Point(dimension) {
  coords: Float32LE[dimension]
}

points: soa Point(3)[1000]  # OK: instant-parse, simple structure
```

Records with variable-size fields work if dependencies follow field order:

```sddl
record VariableItem() {
  size: UInt16LE,        # Array 0
  data: Bytes(size)      # Array 1: depends on earlier field
}

items: soa VariableItem[100]  # OK: size array determines data array layout
```

### `soa` Limitations

You can combine `soa` with auto-sizing **only if** the array is instant-parse:

```sddl
measurements: soa Measurement[]  # Read until end of scope
```

Auto-sized SOA requires the element type to be instant-parse (all fields must have statically-known sizes). This is because the parser needs to calculate the element count from the remaining bytes, which is only possible when each element has a fixed, known size.

---

## Practical Examples

### Example 1: Time Series Data

```sddl
record TimePoint() {
  timestamp: Int64LE,
  temperature: Float32LE,
  humidity: Float32LE,
  pressure: Float32LE
}

count: UInt32LE

# SOA: all timestamps together, all temperatures together, etc.
measurements: soa TimePoint[count]
```

### Example 2: Custom Image Format

In this custom example, we'll imagine a format where each color plane, named a channel, is stored separately and contiguously, in SOA layout.

```sddl
record ImageHeader() {
  magic: Bytes(4),
  width: UInt32LE,
  height: UInt32LE,
  channels: UInt8,
} pad_align 4

Union Pixel(channels) = {
  case 1: gray: UInt8,
  case 3: record() {
    r: UInt8,
    g: UInt8,
    b: UInt8
  },
  case 4: record() {
    r: UInt8,
    g: UInt8,
    b: UInt8,
    a: UInt8
  }
}

header: ImageHeader where header.magic == "IMGF"

# This format uses structure-of-2D-arrays layout
pixels: soa Pixel(header.channels)[header.height][header.width]
```

**Note:** The anonymous `Record() { ... }` syntax means the fields (`r`, `g`, `b`, `a`) become direct members of `Pixel`, not nested under a named field. When `channels == 3`, a `Pixel` has three direct fields: `r`, `g`, and `b`. This is essential for SOA to work— `soa Pixel[n]` creates three separate arrays (one for each color channel), not a single nested structure array.

Then, `soa` layout specifies that all red channel values are together, all green together, etc.

### Example 3: Mixed-Format Records

```sddl
record RecordHeader() {
  type: UInt8,
  size: UInt16LE
}

record TextRecord(size) {
  text: Bytes(size)
}

record BinaryRecord(size) align(4) {
  expect size % 4 == 0
  data: Bytes(size)
}

# Variable-size, type-dispatched records
record Block(type, size) {
  header: BlockHeader,
  when header.type == 1 { text: TextRecord(header.size) },
  when header.type == 2 { binary: BinaryRecord(header.size) }
}

records: scan GenericRecord[]
```

---

## Performance Considerations

### Instant-Parse vs Scan Performance

Instant-parse arrays can be parsed much faster:

```sddl
# Fast: instant-parse
record Fixed(size) {
  data: Bytes(size)
}
count: UInt32LE
fixed: Fixed(100)[count]  # Size known from parameter
```

```sddl
# Slower: requires scan
record Variable() {
  size: UInt16LE,
  data: Bytes(size)  # Size from local field
}
count: UInt32LE
variable: scan Variable[count]
```

The first example allows parallel processing and zero-copy optimizations. The second requires sequential scanning.

This difference compounds with scale. If `Variable` appears once, the scan cost is small. If it appears 50,000 times in an array, the runtime must evaluate `size` 50,000 times and cannot jump directly to record `i` without walking through the previous `i - 1` records. For compressors and other downstream tools, that means lower throughput and weaker opportunities for parallelism.

---

## Summary

Arrays cover a few different patterns: fixed counts (`Type[count]`), parameterized counts that remain instant-parse, auto-sized arrays (`Type[]`) that read to the end of scope, and structure-of-arrays layouts (`soa Type[count]`). Auto-sized arrays require `scan` when element sizes depend on parsed data, while instant-parse arrays can be processed randomly or in parallel. Choose the form that matches the source format: constants or parameters when you know the length up front, auto-sized arrays when the file omits counts, and `soa` when downstream tooling benefits from field-wise storage.

---

## Where to Go Next

- **[Alignment and Padding](alignment-padding.md)** for padding directives that interact with arrays.
- **[Conditional & Variant Data](conditional-variant.md)** if array elements are optional or variant.
- **[Variables and Expressions](variables-expressions.md)** to compute sizes and indices.
