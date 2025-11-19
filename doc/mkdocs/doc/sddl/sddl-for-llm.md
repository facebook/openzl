# SDDL (Simple Data Description Language) — Specification v0.6

SDDL describes binary file formats that can then be exploited by a downstream process, such as compression or transformation.
This document is meant to be read by a LLM, to teach it how to write an SDDL specification.

---

## **1. Core Syntax**

### **1.1 Lexical Rules**

* At root level, statements (`var`, `expect`, field use, type declaration) are newline-terminated.
* Within any block `{}`, `()`, or `[]`, items are comma-separated; trailing commas allowed.
* `#` starts an end-of-line comment.
* Identifiers follow `[A-Za-z_][A-Za-z0-9_]*`.
* Field names must be unique within a record; `_` may repeat for throwaways.
* `var` names must be unique within their scope.

---

## **2. Basic Types**

* `Int8`, `Int16LE`, `Int32LE`, `Int64LE` — signed (little-endian)
* `Int16BE`, `Int32BE`, `Int64BE` — signed (big-endian)
* `UInt8`, `UInt16LE`, `UInt32LE`, `UInt64LE` — unsigned (little-endian)
* `UInt16BE`, `UInt32BE`, `UInt64BE` — unsigned (big-endian)
* `Float16LE`, `Float32LE`, `Float64LE` — IEEE floats (little-endian)
* `Float16BE`, `Float32BE`, `Float64BE` — IEEE floats (big-endian)
* `BFloat16LE`, `BFloat16BE` — Google bfloat16
* `Bytes(n)` — untyped blob of `n` bytes

No global endian default — all multi-byte primitives must specify `LE` or `BE`.
Primitive types are always **instant-parse**.

---

## **3. Instant-Parse Model**

Every type has an inferred property describing its **layout determinism**:

| Property          | Meaning                                                                                       |
| ----------------- | --------------------------------------------------------------------------------------------- |
| **instant-parse** | All field offsets, sizes, and alignments can be computed from parameters and constants alone. |
| **requires scan** | Layout or length depends on data parsed earlier or on delimiter search.                       |

### **3.1 When a Type Requires a Scan**

A construct requires a sequential scan if **any** of the following hold:

* A field size, offset, or branching depends on a **previously parsed field**.
* It uses `Bytes until ...` or another delimiter-based construct.
* It contains `expect` or `where` predicates that reference local fields.
* It invokes `current_position()` or any state-dependent function.
* It contains a member that itself requires a scan.
* It has an auto-sized array (`Type[]`).

Instant-parse status is **transitive**: a record, union, or array is instant-parse only if all its components are instant-parse.

---

### **3.2 Enforcing Instant-Parse**

Use `@instant_parse` to *require* that a record, union, or field be instantly parsable.
If the compiler cannot prove this, it emits a **compile-time error** with a diagnostic explanation.

```sddl
@instant_parse
Record Header(limit) = {
  expect limit <= 4096,
  version: Int16LE
}
```

If a non-instant-parse construct (for example, `Bytes(length)` depending on a local field) is added later, compilation fails:

> Record `Header` is not instant-parse: field `data` depends on local field `length`.

---

### **3.3 Derived Checks**

* `@instant_parse` applies recursively to subfields.
* Arguments to `align`, `pad_to`, and `pad_align` must be parameter-only constants.
* A field declared under `@instant_parse` may not use `scan` or delimiter parsing.
* Diagnostics must point to the first non-instant cause (e.g. field name and dependency).

---

## **4. Record and Union Definitions**

### **4.1 Fixed-Size Records**

```sddl
Record Header() = {
  magic: Bytes(4),
  version: Int32LE,
  flags: Int16LE,
}

Record Block(size) = {
  header: Header,
  block_id: Int32LE,
  data: Bytes(size)
}
```

---

### **4.2 Padding and Alignment**

* `align(n) T` — start each instance of `T` at an `n`-byte boundary relative to the enclosing scope.
  Arrays of aligned types may include inter-element padding.
* `pad_to n` — extend the record to exactly `n` bytes; if smaller ⇒ **format error**.
* `pad_align n` — round total record size up to a multiple of `n`.
* If both are used, `pad_align` follows `pad_to`.
* Padding bytes are "don't care."

```sddl
Record VariableHeader(header_size) = {
  base: Bytes(20)
} pad_to header_size

Record Scanline(width) = {
  pixels: Pixel[width]
} pad_align 4
```

---

### **4.3 Delimiter-Based Parsing**

Delimiter-based fields are **never instant-parse**.

```sddl
text_header: Bytes until "\r\n\r\n"
zero_terminated: Bytes until 0x00
double_zero: Bytes until [0x00, 0x00]
section: Bytes until "---BOUNDARY---" include_delim
```

Consumes up to the first occurrence of the delimiter; missing delimiter ⇒ **data error**.
Delimiters are ASCII sequences or explicit byte arrays.

---

### **4.4 Unions**

```sddl
Union Payload(kind, size) = {
  case 1: Image(size),
  case 2, 3: Audio(size),
  case 0x10..0x1F: BinaryBlob(size),
  default: Raw(size)
}
```

Rules:

* The first parameter is the **dispatch selector**.
* `case` may list literals or ranges; overlapping ranges ⇒ **format error**.
* No match without `default` ⇒ **data error**.
* A union is **instant-parse** only if its selector is a parameter or constant and all arms are instant-parse.

---

### **4.5 Conditional Fields**

**Single field form:**

```sddl
Record Data(has_id, include_meta) = {
  base: Int32LE,
  when has_id > 0 { id: Int64LE },
  when include_meta { meta: Bytes(256) }
}
```

**Block form (multiple statements):**

```sddl
Record Data(has_extended) = {
  base: Int32LE,
  when has_extended {
    ext_field1: Int32LE,
    ext_field2: Int64LE,
    expect ext_field1 > 0
  }
}
```

When using braces `{ }`, there is no `then` keyword. The block can contain multiple fields and statements.

Conditions referencing parameters are instant-parse-safe.
Conditions referencing local fields make the record non-instant-parse.

---

### **4.6 Inline Declarations**

Inline `Record` or `Union` constructs follow the same rules:

```sddl
Record Packet(kind, size, crcWidth) = {
  header: Record() { version: UInt8, flags: UInt16LE },
  payload: Union(kind, size) {
    case 1: Image(size),
    case 2: Audio(size)
  },
  crc: UInt32LE[crcWidth]
}
```

---

## **5. Arrays and Layout**

### **5.1 Fixed-Size Arrays**

```sddl
values: UInt32LE[count]
pixels: Pixel[height][width]
data_blocks: Block(100)[count]
```

An array is instant-parse if its element type is instant-parse and its length expressions depend only on parameters.

---

### **5.2 Auto-Sized Arrays**

```sddl
auto_sized: Header[]          # repeat to end of scope
auto_sized_2d: Pixel[][width]
```

Auto-sized arrays are inherently non-instant-parse.
If an element type requires scanning, `scan` must be used:

```sddl
scan VariableData[]
scan TextRecord[]
```

---

### **5.3 Structure-of-Arrays Layout**

```sddl
Record IndexAndData = { index: Int64LE, data: DataBlock }

particles: soa IndexAndData[count]
```

`soa` means that each top-level field of the element type is stored as a contiguous array.

Rules:

* Element type must be instant-parse.
* Element type must not contain `var` creation or `expect` statements.
* Auto-sized `soa T[]` is valid only for instant-parse elements.
* Violation ⇒ compile-time error.

---

## **6. Enumerations**

```sddl
enum FileType { TEXT = 1, IMAGE = 2, AUDIO = 3 }
enum Mode { READ = 1 << 0, WRITE = 1 << 1, EXEC = 1 << 2 }
```

Enum constants are accessed as `EnumType.CONSTANT` and evaluate to 64-bit signed integer literals.

---

## **7. Variables and Expressions**

```sddl
header: Header
var data_size = header.size
payload: Bytes(data_size)
```

`var` is immediate and immutable; it may reference previously parsed fields.
Such dependencies make the enclosing construct **non-instant-parse** if those variables influence later layout.

Expressions follow C11 precedence.
All integers are 64-bit signed; overflow or division by 0 ⇒ **format error**.

---

### **7.1 Switch Expressions**

```sddl
var block_size = switch version {
  case 1: 512,
  case 2, 3: 1024,
  case 4..10: 2048,
  default: 4096
}
```

* Overlapping ranges ⇒ **format error**
* If no case match and no `default` ⇒ **data error**

---

## **8. Validation**

### **8.1 `expect`**

```sddl
expect header.magic == [0x50,0x4B,0x03,0x04]
expect header.version >= 20
expect format == 1 or format == 2
```

`expect` evaluates when all referenced names exist.
Failure ⇒ data mismatch.
If the expression references only parameters or constants, it is instant-parse-safe.
Otherwise it marks the construct as non-instant-parse.

### **8.3 Enum Membership (`in`)**

Test if a value is in an enum set:

```sddl
enum WaveFormat { PCM = 1, IEEE_FLOAT = 3, EXTENSIBLE = 0xFFFE }

expect audio_format in WaveFormat  # True if audio_format is 1, 3, or 0xFFFE
```

The `in` operator works only with enum types and is typically used within `expect` statements.

### **8.2 `where`**

A shorthand for immediate field validation:

```sddl
size: UInt16LE where (size <= 1024)
```

Allowed everywhere; if it references local fields, it breaks instant-parse.

---

## **9. Standard Functions**

All functions are pure and return 64-bit signed integers.
Overflow ⇒ **format error**.

| Function               | Description           |
| ---------------------- | --------------------- |
| `abs(x)`               | absolute value        |
| `min(a,b)`, `max(a,b)` | comparisons           |
| `clamp(l,x,h)`         | bounded value         |
| `between(l,x,h)`       | range check           |
| `sgn(x)`               | sign function         |
| `ceil_div(x,d)`        | ceil division         |
| `align_up(x,a)`        | alignment helper      |
| `sizeof(T())`          | static size of record |
| `size(f)`              | parsed byte size      |
| `current_position()`   | parser position       |
| `scope_remaining()`    | bytes to end of scope |

---

## **10. Annotations**

Annotations never change baseline parsing; they are advisory or enforce constraints.

```sddl
@chunk_size 128 kb
expect var > 1 @err_msg "Complementary message"
@instant_parse              # enforce static layout
```

Annotations may appear on types, fields, or statements.

---

## **11. Error Classes**

| Type                        | Meaning                                                                                                                                    |
| --------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| **Format error**            | Structural contradiction or invalid expression (`pad_to` smaller than current size, overlapping union cases, zero-size element, overflow). |
| **Data error**              | Parsed bytes do not match expected structure (missing delimiter, invalid value, unrecognized case).                                        |
| **Instant-parse violation** | A construct annotated `@instant_parse` is not instantly parsable; compiler explains the dependency chain.                                  |

---

## **12. Progressive Example**

```sddl
@instant_parse
Record Header() = {
  magic: Bytes(4),
  version: Int16LE,
  packet_count: Int32LE,
  flags: Int16LE
}

header: Header
expect header.magic == "DPKT"
expect between(1, header.version, 3)

var pkt_count = header.packet_count
var has_crc   = header.flags & 0x01

Record Packet(include_crc) = {
  id: Int32LE,
  size: Int16LE,
  when include_crc { checksum: Int32LE }
}

packets: Packet(has_crc)[pkt_count] @instant_parse

Record VariableBlock() = {
  size: UInt16LE,
  data: Bytes(size)
} # non-instant-parse

blocks: scan VariableBlock[]
```

---

## **13. Principles**

* **Readability** — data structure mirrors its bytes.
* **Determinism** — one description ⇒ one layout.
* **Performance visibility** — instant-parse vs. scan-required is explicit and enforceable.
* **Diagnostics** — errors precisely indicate non-instant constructs.
