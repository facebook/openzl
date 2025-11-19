# Best Practices

*Chapter 9 - Effective SDDL specification design*

This chapter collects practical advice for writing clear, maintainable, and effective SDDL specifications. These guidelines come from experience writing specifications for real-world formats and reflect common pitfalls to avoid.

This chapter assumes familiarity with SDDL syntax and constructs. If you need a refresher on records, types, fields, or other language elements, consult the [Language Elements Overview](core-concepts.md#language-elements-overview) first.

---

## Endianness is Required

Multi-byte types require explicit endianness:

```sddl
# WRONG: No such type
value: Int32

# CORRECT
value: Int32LE
```

Single-byte types (`UInt8`, `Int8`) don't need endianness.

---

## Use Parameters for Reusability

Parameters make records reusable and maintain instant-parse status:

```sddl
Record Packet(max_payload) = {
  header: Bytes(12),
  payload: Bytes(max_payload)
}

# Instant-parse with different sizes
small_packet: Packet(64)
large_packet: Packet(1024)
```

---

## Validate Early

Use `where` for immediate field validation:

```sddl
Record Header() = {
  magic: Bytes(4) where (magic == "IMGF"),
  version: UInt16LE where (version >= 1 and version <= 5)
}
```

For more complex validation logic, use `expect` statements:

```sddl
Record Header() = {
  width: UInt32LE,
  height: UInt32LE,

  var total_pixels = width * height,
  expect total_pixels <= 100000000  # Derived constraint
}
```

**Important:** Validation using `where` or `expect` on local fields requires scanning. Removing these checks could make the record instant-parse:

```sddl
# With validation: requires scan
Record Validated() = {
  magic: Bytes(4) where (magic == "IMGF")
}

# Without validation: instant-parse
Record Unvalidated() = {
  magic: Bytes(4)
} @instant_parse
```

The trade-off is between safety and instant-parse status.

---

## Design for Format Evolution

Include and track version information when available:

```sddl
Record MyFormat() = {
  magic: Bytes(4),
  version: UInt16LE,

  # V1 fields
  base_data: Bytes(100),

Record Data(version) = {
  base: Int32LE,
  when version >= 2 { extended_data: ExtendedData },

  # Additional fields
  when version >= 3 { metadata: Metadata }
}
```

This makes it easier to follow format evolutions.

---

## Name Things Clearly

Use descriptive names:

```sddl
# GOOD
Record ImageHeader() = {
  width: UInt32LE,
  height: UInt32LE,
  bits_per_pixel: UInt8
}

# AVOID
Record ImageHeader() = {
  w: UInt32LE,  # Unclear
  h: UInt32LE,
  bpp: UInt8    # Non-obvious acronym
}
```

Naming conventions:
- **Types:** PascalCase (e.g., `Record BlockHeader`)
- **Fields:** snake_case (e.g., `block_size`)
- **Enums:** UPPER_CASE (e.g., `enum Status { ACTIVE = 1 }`)

---

## Document Non-Obvious Decisions

Add comments explaining format choices:

```sddl
# PNG chunk structure as per RFC 2083
Record PNGChunk() = {
  length: UInt32BE,      # Byte count of data field only
  type: Bytes(4),        # ASCII chunk type code
  data: Bytes(length),
  crc: UInt32BE          # CRC-32 of type + data fields
}
```

---

## Organize Complex Formats

Break large formats into reusable components:

```sddl
# Common components
Record Timestamp() = {
  seconds: Int64LE,
  nanos: UInt32LE
}

Record UUID() = {
  bytes: Bytes(16)
}

# Compose them
Record Event() = {
  id: UUID,
  occurred_at: Timestamp,
  data: EventData
}
```

---

## Understand `pad_to` vs `pad_align`

**`pad_to n`:** Record must be exactly n bytes. Format error if naturally larger.

```sddl
Record Fixed() = {
  data: Bytes(10)
} pad_to 16  # Always 16 bytes (or error if data > 16)
```

**`pad_align n`:** Round size up to multiple of n bytes.

```sddl
Record Aligned() = {
  data: Bytes(10)
} pad_align 8  # Rounds 10 up to 16 (next multiple of 8)
```

---

## Avoid Union Case Overlaps

Overlapping union cases cause format errors:

!!! danger "Wrong"
    ```sddl
    Union Bad(type) = {
      case 1..10: TypeA,
      case 5..15: TypeB,  # ERROR: 5-10 overlap!
    }
    ```

**Correct:**
```sddl
Union Good(type) = {
  case 1..10: TypeA,
  case 11..20: TypeB,
  default: TypeC
}
```

---

## Use `_` for Unused Fields

You can reuse `_` for throwaway fields:

```sddl
Record Data() = {
  _: Bytes(4),     # Skip padding
  value: Int64LE,
  _: Bytes(4),     # Skip more padding (OK to reuse _)
}
```

Regular field names must be unique.

---

## Consider Alignment Requirements

If your format has alignment requirements, describe them explicitly:

```sddl
Record Aligned() = {
  id: UInt8,
  _: Bytes(7),                    # Explicit padding
  timestamp: align(8) Int64LE,    # Must be 8-byte aligned
  value: Float64LE
}
```

---

## Test with Real Data

Create test cases covering:

- **Minimum valid case:** Smallest possible valid input
- **Maximum valid case:** Largest/most complex valid input
- **Boundary conditions:** Values at limits
- **Invalid cases:** Inputs that should be rejected

Test your SDDL description against actual files in the wild to ensure accuracy.

---

## Summary

**Key Guidelines:**

- Always specify endianness for multi-byte types
- Validate fields immediately after parsing them
- Use parameters to maintain instant-parse when possible
- Design for format evolution from the start
- Name things clearly and document non-obvious decisions
- Understand `pad_to` vs `pad_align`
- Avoid overlapping union cases
- Test with real data

**Remember:** SDDL describes existing binary formats. Focus on accurately describing the format as it exists, not on optimizing or transforming it.

---

## Where to Go Next

- **[Real-World Formats](real-formats.md)** to see these practices applied in complete specs.
- **[Reference](reference.md)** when you need exact syntax and function signatures.
