# Best Practices

*Chapter 10 - Practical guidelines for writing effective SDDL format descriptions*

This chapter consolidates lessons learned from real-world SDDL usage and provides actionable advice for creating maintainable, performant format descriptions.

---

## Format Design Guidelines

### Start with Instant-Parse When Possible

**DO:**
```sddl
@instant_parse
Record Header(size) = {
  magic: Bytes(4),
  version: Int16LE,
  data: Bytes(size)  # size is a parameter
}
AVOID:

Record Header() = {
  magic: Bytes(4),
  version: Int16LE,
  size: Int32LE,
  data: Bytes(size)  # depends on parsed field - requires scan
}
Why: Instant-parse formats enable parallel processing, memory mapping, and better compression performance.

Use Parameters to Maintain Instant-Parse
Pass dynamic values as parameters rather than parsing them from data when possible.

Good:

Record Block(expected_size) = {
  header: Header,
  expect header.size == expected_size,
  data: Bytes(expected_size)
}
Design for Alignment
Consider hardware alignment requirements early:

Record Particle = {
  id: Int64LE,        # 8-byte aligned
  position: Float32LE[3],  # Consider padding
  _: Bytes(4)         # Explicit padding for next particle
}
Version Your Formats
Build version handling into your format from the start:

Record MyFormat(version) = {
  when version >= 1 then field_v1: Data,
  when version >= 2 then field_v2: EnhancedData,
  when version >= 3 then field_v3: NewFeature
}
Performance Optimization
Choose the Right Layout
Array-of-Structures (AoS) - Default
Best for: Sequential access, small element counts

particles: Particle[count]  # Each particle stored contiguously
Structure-of-Arrays (SoA)
Best for: Columnar access, SIMD operations, compression

particles: soa Particle[count]  # Each field stored as array
Minimize Scanning Overhead
Pattern: Group variable-length data at the end

Record Message() = {
  # Fixed-size fields first (instant-parse)
  id: Int32LE,
  timestamp: Int64LE,
  length: Int32LE,

  # Variable-length data last (requires scan)
  body: Bytes(length)
}
Align for Cache Efficiency
Record CacheLineFriendly() = {
  hot_data: Bytes(64)  # Frequently accessed
} pad_align 64  # Align to cache line boundary
Use Appropriate Types
# DON'T over-specify precision
count: Int64LE  # If count never exceeds 65535

# DO use the right size
count: Int16LE  # Smaller type = better compression
Code Organization
Modular Type Definitions
Break complex formats into reusable components:

# Common types
Record Timestamp() = {
  seconds: Int64LE,
  nanos: Int32LE
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
Naming Conventions
Types: PascalCase - Record BlockHeader()
Fields: snake_case - block_size: Int32LE
Enums: UPPER_CASE - enum Status { PENDING = 0, ACTIVE = 1 }
Parameters: snake_case - Record Data(max_size)
Use Meaningful Field Names
GOOD:

Record ImageHeader() = {
  width: Int32LE,
  height: Int32LE,
  bits_per_pixel: Int8
}
BAD:

Record ImageHeader() = {
  w: Int32LE,  # Unclear abbreviation
  h: Int32LE,
  bpp: Int8    # Non-obvious acronym
}
Document Your Intentions
# PNG chunk structure as per RFC 2083
Record PNGChunk() = {
  length: Int32BE,           # Byte count of data field
  type: Bytes(4),            # ASCII chunk type code
  data: Bytes(length),       # Chunk-specific data
  crc: Int32BE               # CRC-32 of type + data
}
Validation Strategy
Validate Early
Place critical expect statements immediately after parsing the relevant fields:

Record Header() = {
  magic: Bytes(4),
  expect magic == [0x89, 0x50, 0x4E, 0x47],  # Check immediately

  version: Int16LE,
  expect version >= 1 and version <= 10
}
Provide Helpful Error Messages
expect header.version <= MAX_VERSION
  @err_msg "Unsupported version. Please upgrade the parser."

expect size <= MAX_SIZE
  @err_msg "File too large. Maximum size is 2GB."
Use where for Field-Level Constraints
Record Config() = {
  port: Int16LE where (port >= 1024 and port <= 65535),
  timeout_ms: Int32LE where (timeout_ms > 0)
}
Common Pitfalls
1. Forgetting Endianness
❌ WRONG:

value: Int32  # This doesn't exist!
✅ CORRECT:

value: Int32LE  # Explicit endianness
2. Inadvertent Scan Dependencies
❌ WRONG:

@instant_parse  # Will fail to compile!
Record Data() = {
  size: Int32LE,
  buffer: Bytes(size)  # Depends on parsed field
}
✅ CORRECT:

@instant_parse
Record Data(size) = {
  buffer: Bytes(size)  # Parameter-based
}
3. Misunderstanding pad_to vs pad_align
Record Fixed() = {
  data: Bytes(10)
} pad_to 16       # Error if data > 16 bytes

Record Aligned() = {
  data: Bytes(10)
} pad_align 8     # Always rounds up to multiple of 8
4. Overlapping Union Cases
❌ WRONG:

Union Data(type) = {
  case 1..10: TypeA,
  case 5..15: TypeB,  # Overlap with 5-10!
  default: TypeC
}
5. Reusing Field Names
❌ WRONG:

Record Bad() = {
  temp: Int32LE,
  temp: Int32LE,  # Error: duplicate field name
}
✅ CORRECT:

Record Good() = {
  _: Int32LE,  # Throwaway field
  _: Int32LE,  # _ can be repeated
}
Testing and Validation
Create Representative Test Data
Minimum valid case: Smallest possible valid file
Maximum valid case: Largest/most complex valid file
Boundary conditions: Values at limits
Invalid cases: Files that should be rejected
Validate Against Real Files
# Pseudo-command
sddl-validate my-format.sddl sample-files/*.bin
Test Both Parsing Directions
If your format is bidirectional, test both:

Parse binary → structure
Generate structure → binary → structure (round-trip)
Use Fuzzing
# Pseudo-command
sddl-fuzz my-format.sddl --iterations 10000
Migration Strategies
Adding New Fields
Use when for backward compatibility:

Record Config(version) = {
  # v1 fields
  setting_a: Int32LE,

  # v2 fields (optional for v1 files)
  when version >= 2 then setting_b: Int64LE,

  # v3 fields
  when version >= 3 then setting_c: Float32LE
}
Deprecating Fields
Mark deprecated fields clearly:

Record OldFormat(version) = {
  # DEPRECATED in v3: use new_field instead
  when version < 3 then old_field: Int32LE,

  when version >= 3 then new_field: Int64LE
}
Format Evolution Checklist
 Increment version number
 Add version check for new features
 Maintain backward compatibility
 Update documentation
 Test with old and new files
 Consider migration tools for existing data
Debugging Strategies
Start Simple
Create minimal test cases:

# Minimal reproducer
Record Minimal() = {
  field_causing_issue: Int32LE
}
Add Temporary Diagnostics
var pos = current_position()
# ... parse some data ...
var size = current_position() - pos
expect size <= 1024 @err_msg "Unexpected size"
Validate Incrementally
Add expect statements to narrow down issues:

Record Debug() = {
  header: Header,
  expect current_position() == 16,  # Should be here

  data: Data,
  expect size(data) > 0,  # Data shouldn't be empty
}
Use Hex Dumps
Compare expected vs actual byte layout:

# Pseudo-command
sddl-dump my-format.sddl problematic-file.bin
Performance Checklist
Before finalizing your format:

 Use @instant_parse wherever possible
 Align data structures appropriately
 Consider SoA layout for arrays
 Group fixed-size fields before variable-size
 Use smallest appropriate type sizes
 Minimize delimiter-based parsing
 Test with compression pipeline
 Profile with real-world data sizes
Quick Reference: Dos and Don'ts
✅ DO
Always specify endianness explicitly
Use parameters to avoid scan dependencies
Validate critical fields immediately
Document format decisions
Test with diverse data sets
Version your formats from the start
Use @instant_parse to enforce performance
❌ DON'T
Assume a default endianness
Mix reading and validation arbitrarily
Use ambiguous field names
Ignore alignment considerations
Forget to handle format evolution
Create circular dependencies
Overlook the instant-parse vs scan distinction
Further Reading
Understanding Instant-Parse - Deep dive into performance
Advanced Layout Control - Alignment and SoA
Language Reference - Complete syntax reference
