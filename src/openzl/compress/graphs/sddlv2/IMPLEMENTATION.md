# OpenZL Execution Engine — Implementation Plan

> **Status**: Phases 1-2 Complete ✅ | **Priority**: Segment Generation
> **Spec Version**: v0.2
> **Last Updated**: 2025-11-06

---

## Overview

This document outlines the incremental implementation plan for the OpenZL Execution Engine VM. The VM's **primary purpose** is to:

1. **Traverse input buffer** exactly once
2. **Generate tagged segments** over byte ranges
3. **Output segment list** for downstream processing (compression, etc.)

Everything else (arithmetic, comparisons, etc.) is a **support feature** for segment generation.

---

## Revised Implementation Strategy

**Original mistake**: Building arithmetic/comparison operations before having any segments to test with.

**New priority**: **Get to segments ASAP**, then add supporting operations as needed.

---

## Implementation Phases

### ✅ Phase 1: Foundation — **COMPLETE**

**Goal**: Stack, values, types.

**Delivered**:
- Value system (`I64`, `Tag`, `Type`)
- Type system (19 primitive types)
- Stack operations with bounds checking
- 7 tests, all passing ✅

---

### ✅ Phase 2: Arithmetic — **COMPLETE**

**Goal**: Address/size computation support.

**Delivered**:
- `add`, `sub`, `mul`, `div`, `mod`, `abs`, `neg`
- Overflow detection
- 25 tests, all passing ✅

**Note**: Complete but not critical path. Refocusing on segments.

---

### Phase 3: Input Buffer (NEXT — HIGHEST PRIORITY)

**Goal**: Read data from input files.

**Minimal Implementation**:

```c
typedef struct {
    const uint8_t* data;
    size_t file_size;
    size_t current_pos;  // Cursor for sequential reading
} openzl_input_buffer;
```

**Operations to implement**:
1. `current_pos` - Push cursor position to stack
2. `load.u8` - Load single byte (simplest load)
3. Maybe: `load.i32le`, `load.i64le` for size/offset reading

**Testing**:
- Create buffer from byte array
- Push `current_pos`
- Load byte at address
- Bounds checking

**Deliverable**: Can read from input buffer.

---

### Phase 4: Simple Byte Segments (CORE FUNCTIONALITY)

**Goal**: Generate unspecified byte segments (no type info).

**Segment Structure**:
```c
typedef struct {
    uint32_t tag;           // Segment identifier
    size_t start_pos;       // Start offset in input
    size_t size_bytes;      // Length
} openzl_segment_simple;
```

**Segment List**:
```c
typedef struct {
    openzl_segment_simple* items;
    size_t count;
    size_t capacity;
} openzl_segment_list;
```

**Operation**:
- `segment_create_simple` - Create untyped byte blob

**Stack Contract**:
```
Tag:I64  size:I64
  ── segment_create_simple ──>  (nothing, segment recorded)
```

**Side Effects**:
- Advances `current_pos += size`
- Appends segment to list
- Bounds checking: `current_pos + size ≤ file_size`

**Testing**:
- Create single segment
- Create multiple segments
- Bounds checking
- Verify segment list output

**Deliverable**: **END-TO-END SEGMENT GENERATION!** 🎉

---

### Phase 5: Tag Registry

**Goal**: Enforce type consistency when reusing tags.

**Tag Registry**:
```c
// Initially: just track tags without types
typedef struct {
    uint32_t* tags;
    size_t count;
} openzl_tag_registry;
```

**Enforcement**:
- First use of tag: register it
- Subsequent uses: must match (later: type checking)

**Testing**:
- Tag reuse (valid)
- Tag conflict detection (later, with types)

**Deliverable**: Tag consistency enforcement.

---

### Phase 6: Type Table & Typed Segments

**Goal**: Add type information to segments.

**Type Table**:
```c
typedef struct {
    openzl_type* types;
    size_t count;
} openzl_type_table;
```

**Operations**:
- `type.const <index>` - Push type from table

**Enhanced Segment**:
```c
typedef struct {
    uint32_t tag;
    size_t start_pos;
    size_t size_bytes;
    openzl_type type;       // Now has type!
} openzl_segment;
```

**Stack Contract**:
```
Tag:I64  size:I64  Type
  ── segment_create ──>  (segment with type)
```

**Testing**:
- Type table creation
- Type retrieval
- Typed segment creation
- Tag+type consistency

**Deliverable**: Typed segments.

---

### Phase 7: Array Segments

**Goal**: Arrays with element type validation.

**Operations**:
- `array_segment_create` - Single element type array

**Stack Contract**:
```
Tag:I64  size_bytes:I64  Type(elem_type)
  ── array_segment_create ──>
```

**Validation**:
```
elem_size = SDDL2_type_size(elem_type.kind) * elem_type.width
assert(size_bytes % elem_size == 0)
```

**Testing**:
- Valid array (size is multiple of element size)
- Invalid array (size mismatch)
- Various element types

**Deliverable**: Array segments with validation.

---

### Phase 8: Array of Structures (AoS)

**Goal**: Multi-field structure arrays.

**Operations**:
- `soa_segment_create` - Struct with N fields

**Stack Contract**:
```
Tag  size_bytes  Type₀  Type₁  ...  Typeₙ₋₁  N:I64
  ── soa_segment_create ──>
```

**Validation**:
```
struct_size = Σ SDDL2_type_size(field_i)
elem_count = size_bytes / struct_size
assert(size_bytes % struct_size == 0)
```

**Testing**:
- Single-field struct (degenerate case)
- Multi-field structs
- Size validation
- Field ordering

**Deliverable**: AoS segments.

---

### Phase 9+: Supporting Operations (As Needed)

**Add only when needed for real use cases**:

- **Comparisons** (`eq`, `ne`, `lt`, `le`, `gt`, `ge`) - For conditional segments
- **Stack ops** (`dup`, `swap`, `drop_if`) - For complex stack manipulation
- **More loads** (`load.i16le`, `load.f32le`, etc.) - As needed
- **Chunking** - When segments get large
- **Output/logging** - For debugging

**Principle**: Don't implement until there's a concrete need.

---

## File Structure

```
src/openzl/compress/graphs/sddlv2/
├── sddl2_vm.h              # VM interface
├── sddl2_vm.c              # VM implementation
├── IMPLEMENTATION.md       # This file

tests/compress/graphs/sddlv2/
├── sddl2_vm_test.c         # Phase 1 tests ✅
├── sddl2_arithmetic_test.c # Phase 2 tests ✅
├── sddl2_input_test.c      # Phase 3 tests (next)
├── sddl2_segments_test.c   # Phase 4-8 tests (core!)
└── sddl2_integration_test.c # Full VM tests (later)
```

---

## Current Status

### ✅ Completed
- **Phase 1**: Foundation (Stack + Values) - 7 tests ✅
- **Phase 2**: Arithmetic - 25 tests ✅

### 🎯 Next Up (Substance!)
- **Phase 3**: Input Buffer (read data)
- **Phase 4**: Simple Byte Segments ← **This is the payoff!**

### 📋 Future
- Phase 5: Tag Registry
- Phase 6: Typed Segments
- Phase 7: Array Segments
- Phase 8: AoS Segments
- Phase 9+: Supporting ops as needed

---

## Testing Strategy

### Priority: Segment Generation Tests

**Phase 4 (Simple Segments) test cases**:
```c
// Test 1: Single segment
input = [0x01, 0x02, 0x03, 0x04]
program:
  push 100     // tag
  push 4       // size
  segment_create_simple
result:
  segments[0] = {tag: 100, start: 0, size: 4}
  current_pos = 4

// Test 2: Multiple segments
input = [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF]
program:
  push 10, push 2, segment_create_simple  // seg 1: bytes 0-1
  push 20, push 3, segment_create_simple  // seg 2: bytes 2-4
  push 30, push 2, segment_create_simple  // seg 3: bytes 5-6
result:
  3 segments covering entire input

// Test 3: Bounds checking
input = [0x01, 0x02, 0x03]
program:
  push 100, push 10, segment_create_simple  // Size exceeds file!
result:
  ERROR: Bounds violation
```

---

## Key Insight

**The VM's value is in segment generation**. Everything else supports that goal.

- Arithmetic? Only matters if computing segment sizes/positions
- Comparisons? Only matters if conditionally creating segments
- Stack ops? Only matters if manipulating segment parameters

**Focus**: Get a working segment generator, then add operations as real programs need them.

---

**Last Updated**: 2025-11-06
**Next Milestone**: Phase 3 (Input Buffer) → Phase 4 (Simple Segments)
