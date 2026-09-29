# Instant-Parse

An **instant-parse** record is one whose layout — every field's offset and size — can be computed from its parameters and constants alone, without examining the data being parsed. A record that is not instant-parse is said to **require scanning**: its layout depends on values that are only known once the data has been read.

## Instant-Parse vs. Requires-Scan

The distinction comes down to whether a field's size or presence depends on a **parameter** (known upfront) or on a **previously parsed field** (only known after reading data).

**Instant-parse** — sizes depend only on parameters:

```sddl
record Header(payload_size) {
  magic: Bytes(4),
  version: UInt16LE,
  payload: Bytes(payload_size)  # size is a parameter, known upfront
}
```

Given `payload_size`, every offset is known before reading a byte: `magic` at 0, `version` at 4, `payload` at 6.

**Requires-scan** — a size depends on a parsed field:

```sddl
record Message() {
  magic: Bytes(4),
  size: UInt16LE,
  payload: Bytes(size)  # size depends on a parsed field, must scan
}
```

Here `payload`'s offset and the total record size cannot be known until `size` has been read.

## The `@instant_parse` Annotation

Records may carry **annotations**, written as `@name` immediately after the record's closing brace. Multiple annotations may be chained (`record ... {} @a @b`). `instant_parse` is the first annotation the compiler acts on. It asserts that the record is instant-parse — **the compiler errors if the record actually requires scanning:**

```sddl
record Header(has_checksum) {
  magic: Bytes(4),
  version: UInt16LE,
  when has_checksum { checksum: UInt32LE }
} @instant_parse
```

If the record's layout depends on parsed data, compilation fails:

```sddl
record Message() {
  magic: Bytes(4),
  size: UInt16LE,
  payload: Bytes(size)  # depends on a parsed field
} @instant_parse
```

```
semantic error: Record is annotated @instant_parse but is not instant-parse (its layout depends on parsed data).
```

The annotation works on both named and anonymous records:

```sddl
: record() {
  x: Int32LE,
  y: Int32LE
} @instant_parse
```

### Why Use It?

- **Documentation:** makes the instant-parse requirement explicit at the definition site.
- **Enforcement:** prevents a later edit from silently introducing a scan dependency. Scan dependencies are significantly less efficient than instant-parse.
