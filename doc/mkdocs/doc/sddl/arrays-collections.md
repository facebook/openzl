# Arrays and Collections

*Chapter 5 - Working with repeated data*

Arrays are fundamental to binary format descriptions. Most real-world formats contain sequences of repeated structures—pixels in images, samples in audio, records in databases. This chapter explores how SDDL handles arrays, from simple fixed-size collections to complex structure-of-arrays layouts.

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
Record Image(width, height, channels) = {
  pixels: UInt8[height][width][channels]
}
```

Layout is row-major: the rightmost index varies fastest. In the example above, the three channel values for pixel (0,0) come first, followed by channels for pixel (0,1), and so on.

### Arrays of Records

Arrays work with any type, including records:

```sddl
Record Point() = {
  x: Float32LE,
  y: Float32LE,
  z: Float32LE
}

Record PointCloud(num_points) = {
  points: Point[num_points]
}

count: UInt32LE
cloud: PointCloud(count)
```

This creates an array of `Point` structures laid out sequentially in memory.

### Parameterized Element Types

Array elements can themselves be parameterized:

```sddl
Record Block(size) = {
  header: Bytes(4),
  data: Bytes(size - 4)
}

Record FileData(block_size, block_count) = {
  blocks: Block(block_size)[block_count]
}
```

Each `Block` in the array uses the same `block_size` parameter.

---

## Auto-Sized Arrays

When you don't know the array size in advance, use auto-sized arrays.

### Reading Until End of Scope

An array without a size specification repeats until there's no more data:

```sddl
Record Entry() = {
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
Record Container(payload_size) = {
  header: Bytes(16),
  entries: Entry[]                # Reads until payload_size is exhausted
} pad_to payload_size
```

### Combining Fixed and Auto-Sized Dimensions

You can mix fixed and auto-sized dimensions:

```sddl
Record Scanline(width) = {
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
Record VariableEntry() = {
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

## Structure-of-Arrays Layout

By default, arrays store elements sequentially: all fields of element 0, then all fields of element 1, and so on. This is called array-of-structures (AOS).

### Array-of-Structures (Default)

```sddl
Record Particle() = {
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

Sometimes, data can be laid out as structure-of-arrays (SOA).

```sddl
Record Particle() = {
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

This is equivalent to 6 arrays of same size.
Describing them as a single SOA array is useful: it tells clearly that all arrays have the same size,
which can be further exploited for compression opportunities.
It's also compatible with auto-size.

### `soa` Requirements

The element type should be instant-parse and cannot contain `var` declarations or `expect` statements inside the record. These restrictions ensure deterministic layout.

```sddl
Record Point(dimension) = {
  coords: Float32LE[dimension]
}

points: soa Point(3)[1000]  # OK: instant-parse, simple structure
```

You can combine `soa` with auto-sizing:

```sddl
measurements: soa Measurement[]  # Read until end of scope
```

---

## Arrays and Instant-Parse

Whether an array is instant-parse depends on its element type and size specification.

### Instant-Parse Arrays

An array is instant-parse when:
- Its element type is instant-parse
- Its size depends only on parameters or constants

```sddl
@instant_parse
Record Grid(width, height) = {
  cells: Cell[height][width]
}
```

If `Cell` is instant-parse and `width`/`height` are parameters, the entire `Grid` is instant-parse.

### Non-Instant-Parse Arrays

Arrays become non-instant-parse when:
- The element type requires scanning
- The size depends on local fields

```sddl
Record Data() = {
  count: UInt32LE,
  values: Int32LE[count]  # Size depends on local field
}
```

This `Data` record is not instant-parse because `count` is a local field, not a parameter.

### Promoting to Instant-Parse

You can sometimes make an array instant-parse by moving the size to a parameter:

```sddl
Record Data(item_count) = {
  values: Int32LE[item_count]  # Now instant-parse
}

count: UInt32LE
data: Data(count)
```

Now `Data` is instant-parse because `item_count` is known before parsing the record.

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
Record Aligned() = {
  value: UInt8,
  important: align(8) Int64LE  # Must start at 8-byte boundary
}

records: Aligned[100]
```

Each `Aligned` record may include padding after `value` to ensure `important` starts at an 8-byte boundary. The array will include padding between elements to maintain alignment.

### Record Padding

Records with `pad_align` or `pad_to` affect array layout:

```sddl
Record Padded() = {
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
Record Item() = {
  id: Int32LE,
  value: Float32LE
}

count: UInt32LE
items: Item[count]
```

### Pattern 2: Size-Prefixed Blob Array

Store the total byte size, then read elements until that size is consumed.

```sddl
Record Entry() = {
  id: UInt16LE,
  data: Bytes(10)
}

blob_size: UInt32LE
blob: Bytes(blob_size)
entries: Entry[]  # Within the blob scope
```

This limits the array to exactly `blob_size` bytes.

### Pattern 3: Type-Specific Arrays

Different array types based on a flag or version:

```sddl
Record Header() = {
  version: UInt16LE,
  count: UInt32LE
}

header: Header

when header.version == 1 then data_v1: DataV1[header.count]
when header.version == 2 then data_v2: DataV2[header.count]
```

### Pattern 4: Nested Arrays

Arrays of arrays for grid or matrix data:

```sddl
Record Row(width) = {
  cells: Cell[width]
}

Record Grid(width, height) = {
  rows: Row(width)[height]
}
```

### Pattern 5: Chunked Data

Split data into fixed-size chunks:

```sddl
Record Chunk() = {
  data: Bytes(4096)
}

num_chunks: UInt32LE
chunks: Chunk[num_chunks]
```

Each chunk is exactly 4096 bytes, making the data easy to process in blocks.

### Pattern 6: Mixed Fixed and Variable

Combine fixed-size headers with variable-size payloads:

```sddl
Record PacketHeader() = {
  id: UInt32LE,
  payload_size: UInt16LE,
  flags: UInt16LE
}

Record Packet() = {
  header: PacketHeader,
  payload: Bytes(header.payload_size)
}

packets: scan Packet[]  # Variable-size packets until end of file
```

---

## Practical Examples

### Example 1: Image Format

```sddl
Record ImageHeader() = {
  magic: Bytes(4),
  width: UInt32LE,
  height: UInt32LE,
  channels: UInt8,
  _: Bytes(3)  # Padding for alignment
}

Record Pixel(channels) = {
  data: UInt8[channels]
}

header: ImageHeader
expect header.magic == "IMGF"

var num_pixels = header.width * header.height
var channels = header.channels

# Structure-of-arrays for better compression
pixels: soa Pixel(channels)[num_pixels]
```

SOA layout specifies that all red channel values are together, all green together, etc.

### Example 2: Time Series Data

```sddl
Record TimePoint() = {
  timestamp: Int64LE,
  temperature: Float32LE,
  humidity: Float32LE,
  pressure: Float32LE
}

count: UInt32LE

# SOA: all timestamps together, all temperatures together, etc.
measurements: soa TimePoint[count]
```

### Example 3: Mixed-Format Records

```sddl
Record RecordHeader() = {
  type: UInt8,
  size: UInt16LE
}

Record TextRecord(size) = {
  text: Bytes(size)
}

Record BinaryRecord(size) = {
  data: Bytes(size)
}

# Variable-size, type-dispatched records
Record GenericRecord() = {
  header: RecordHeader,
  when header.type == 1 then text: TextRecord(header.size),
  when header.type == 2 then binary: BinaryRecord(header.size)
}

records: scan GenericRecord[]
```

---

## Performance Considerations

### Instant-Parse vs Scan Performance

Instant-parse arrays can be parsed much faster:

```sddl
# Fast: instant-parse
Record Fixed(size) = {
  data: Bytes(size)
}
count: UInt32LE
fixed: Fixed(100)[count]  # Size known from parameter
```

```sddl
# Slower: requires scan
Record Variable() = {
  size: UInt16LE,
  data: Bytes(size)  # Size from local field
}
count: UInt32LE
variable: scan Variable[count]
```

The first example allows parallel processing and zero-copy optimizations. The second requires sequential scanning.

This difference compounds with scale. If `Variable` appears once, the scan cost is small. If it appears 50,000 times in an array, the runtime must evaluate `size` 50,000 times and cannot jump directly to record `i` without walking through the previous `i - 1` records. For compressors and other downstream tools, that means lower throughput and weaker opportunities for parallelism.

## Array Validation

You can validate array properties:

```sddl
count: UInt32LE
items: Item[count]

expect count > 0
expect count <= 10000  # Sanity check on array size
```

---

## Summary

Arrays cover a few different patterns: fixed counts (`Type[count]`), parameterized counts that remain instant-parse, auto-sized arrays (`Type[]`) that read to the end of scope, and structure-of-arrays layouts (`soa Type[count]`). Auto-sized arrays require `scan` when element sizes depend on parsed data, while instant-parse arrays can be processed randomly or in parallel. Choose the form that matches the source format: constants or parameters when you know the length up front, auto-sized arrays when the file omits counts, and `soa` when downstream tooling benefits from field-wise storage.

---

## Where to Go Next

- **[Alignment and Padding](alignment-padding.md)** for padding directives that interact with arrays.
- **[Conditional & Variant Data](conditional-variant.md)** if array elements are optional or variant.
- **[Variables and Expressions](variables-expressions.md)** to compute sizes and indices.
