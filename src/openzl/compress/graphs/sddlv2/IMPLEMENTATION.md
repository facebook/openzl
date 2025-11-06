# OpenZL Execution Engine — Implementation Plan

> **Status**: Phases 1-4 Complete ✅ | **Milestone**: End-to-End Segment Generation Working!
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

## Implementation Strategy

**Priority**: **Get to segments ASAP**, then add supporting operations as needed.

**Key Insight**: We successfully reached end-to-end segment generation in Phase 4, validating the incremental approach.

---

## Implementation Phases

### ✅ Phase 1: Foundation — **COMPLETE**

**Goal**: Stack, values, types.

**Delivered**:
- Value system with consistent `SDDL2_*` naming
- Type system (19 primitive types: U8, I16LE, F32BE, BYTES, etc.)
- Stack operations with bounds checking
- Unified error system: `SDDL2_error` enum
- 7 tests, all passing ✅

**Key Types**:
- `SDDL2_value` - Stack values (I64, Tag, Type)
- `SDDL2_stack` - VM stack structure
- `SDDL2_error` - Unified error codes

**Files**: `sddl2_vm.h`, `sddl2_vm.c`, `sddl2_vm_test.c`

---

### ✅ Phase 2: Arithmetic — **COMPLETE**

**Goal**: Address/size computation support.

**Delivered**:
- 7 operations: `add`, `sub`, `mul`, `div`, `mod`, `abs`, `neg`
- Comprehensive overflow detection using safe integer arithmetic
- Special handling for INT64_MIN edge cases
- 25 tests, all passing ✅

**Use Case**: Computing segment sizes dynamically (e.g., `push 2, push 3, add` → size 5)

**Files**: `sddl2_vm.c` (implementations), `sddl2_arithmetic_test.c`

---

### ✅ Phase 3: Input Buffer — **COMPLETE**

**Goal**: Read data from input buffers.

**Implementation**:

```c
typedef struct {
    const void* data;       // Borrowed pointer (any type)
    size_t size;            // Total size in bytes
    size_t current_pos;     // Cursor for sequential reading
} SDDL2_input_buffer;
```

**Design Decisions**:
- `const void*` instead of `const uint8_t*` for flexibility (accepts mmap, malloc, etc. without casting)
- `size` instead of `file_size` (not all data comes from files)
- Clear lifetime contract: caller owns data, must outlive buffer

**Operations Implemented**:
1. `SDDL2_op_current_pos` - Push cursor position to stack (doesn't advance)
2. `SDDL2_op_load_u8` - Load single byte at address (random access, doesn't advance cursor)
3. `SDDL2_input_buffer_init` - Initialize buffer

**Error Handling**:
- `SDDL2_LOAD_BOUNDS` for out-of-bounds loads
- Negative address rejection
- Type checking (address must be I64)

**Testing**: 18 tests covering:
- Buffer initialization
- current_pos operations
- load.u8 basic operations
- Bounds checking (positive, negative, edge cases)
- Type mismatches
- Combined operations with arithmetic

**Files**: `sddl2_vm.c` (implementations), `sddl2_input_test.c`

**Deliverable**: ✅ Can read from input buffer with comprehensive bounds checking.

---

### ✅ Phase 4: Unspecified Byte Segments — **COMPLETE** 🎉

**Goal**: Generate truly unspecified segments (no tags, no types, just size).

**Segment Structure**:
```c
typedef struct {
    uint32_t tag;           // Tag (0 for unspecified)
    size_t start_pos;       // Start offset in input
    size_t size_bytes;      // Length in bytes
} SDDL2_segment;
```

**Segment List** (dynamic growth):
```c
typedef struct {
    SDDL2_segment* items;   // Dynamic array (grows 2x)
    size_t count;           // Number of segments
    size_t capacity;        // Allocated capacity
} SDDL2_segment_list;
```

**Operation Implemented**:
- `SDDL2_op_segment_create_unspecified` - Create unspecified byte blob

**Stack Contract** (simplified!):
```
size:I64
  ── segment_create_unspecified ──>  (nothing, segment recorded with tag=0)
```

**Side Effects**:
- Advances `buffer->current_pos += size`
- Appends segment to list (tag=0)
- Bounds checking: `current_pos + size ≤ buffer->size`
- Dynamic list growth (starts at 16, doubles when full)

**Design Decisions**:
- **No tag argument** - truly unspecified (tags come in Phase 5)
- **No type argument** - types come in Phase 6
- **Minimal contract** - just size, everything else inferred

**Error Handling**:
- `SDDL2_SEGMENT_BOUNDS` for segments exceeding buffer
- Type checking (size must be non-negative I64)
- Allocation failure handling

**Testing**: 20 tests covering:
- Single/multiple segments
- Zero-size segments
- Dynamic list growth (50+ segments)
- Bounds checking (exceeds, exact boundary, partial)
- Type mismatches (wrong size type, negative size)
- Stack underflow
- Integration with arithmetic

**Example VM Program**:
```c
// Input: "Hello" (5 bytes)
push 5
segment_create_unspecified
// Result: segments[0] = {tag: 0, start: 0, size: 5}
```

**With arithmetic**:
```c
push 2
push 3
add                          // → 5
segment_create_unspecified   // Segment of computed size!
```

**Files**: `sddl2_vm.c` (implementations), `sddl2_segments_test.c`

**Deliverable**: ✅ **END-TO-END SEGMENT GENERATION WORKING!** 🎉

**Total Tests**: 70 tests across all phases, all passing ✅

---

### Phase 5: Tag Registry & Segment Merging

**Goal**: Add explicit tags and automatic segment merging.

**Tag Registry**:
```c
typedef struct {
    uint32_t* tags;
    size_t count;
} SDDL2_tag_registry;
```

**Tagged Segment Operation**:
```c
Stack: tag:I64  size:I64
  ── segment_create_tagged ──>  (nothing, segment recorded or merged)
```

**Automatic Segment Merging** (Option B):
When creating a segment:
1. If last segment has **same tag** as new segment
2. And positions are **consecutive** (no gap)
3. Then **merge** into existing segment instead of creating new one

**Example**:
```c
push 100, push 2, segment_create_tagged  // seg[0]: {tag=100, start=0, size=2}
push 100, push 3, segment_create_tagged  // Merged! seg[0]: {tag=100, start=0, size=5}
push 200, push 1, segment_create_tagged  // seg[1]: {tag=200, start=5, size=1} (new tag)
```

**Benefits**:
- Reduces segment count automatically
- Intuitive: same tag = same semantic meaning
- Efficient: no extra operations needed

**Enforcement**:
- First use of tag: register it
- Subsequent uses: must match same tag

**Testing**:
- Explicit tags on segments
- Automatic merging (consecutive same-tag)
- No merging when tags differ
- No merging with gaps in positions
- Tag conflict detection

**Deliverable**: Tagged segments with automatic merging.

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
- **Phase 3**: Input Buffer - 18 tests ✅
- **Phase 4**: Unspecified Segments - 20 tests ✅

**Total**: **70 tests, all passing!** 🎉

**Milestone**: **END-TO-END SEGMENT GENERATION WORKING!**

### 📋 Next Phases (Progressive Enhancement)
- **Phase 5**: Tag Registry (explicit tags)
- **Phase 6**: Typed Segments (add type info)
- **Phase 7**: Array Segments (element validation)
- **Phase 8**: AoS Segments (multi-field structs)
- **Phase 9+**: Supporting ops as needed (comparisons, chunking, etc.)

---

## Design Decisions Made

### Naming Consistency
All public VM types use `SDDL2_*` prefix:
- `SDDL2_stack`, `SDDL2_error`, `SDDL2_value`, `SDDL2_type`
- `SDDL2_input_buffer`, `SDDL2_segment`, `SDDL2_segment_list`
- Functions: `SDDL2_op_add()`, `SDDL2_op_segment_create_unspecified()`

### Error System
Unified `SDDL2_error` enum with domain-specific codes:
- Stack: `SDDL2_STACK_OVERFLOW`, `SDDL2_STACK_UNDERFLOW`
- Operations: `SDDL2_TYPE_MISMATCH`
- Input: `SDDL2_LOAD_BOUNDS`
- Segments: `SDDL2_SEGMENT_BOUNDS`

### Buffer Design
- `const void* data` - accepts any pointer type without casting
- `size` not `file_size` - generic for all data sources
- Clear lifetime: caller owns data

### Segment Progression
- Phase 4: Unspecified (tag=0, no types)
- Phase 5: Add explicit tags
- Phase 6: Add type information
- Phase 7-8: Add validation (arrays, structs)

---

## Key Achievements

1. ✅ **End-to-end functionality** - VM generates segments from input
2. ✅ **Comprehensive testing** - 70 tests covering all features
3. ✅ **Production quality** - Proper error handling, bounds checking, memory management
4. ✅ **Consistent design** - All types, functions, errors follow `SDDL2_*` naming
5. ✅ **Incremental approach** - Each phase builds on previous, fully tested before moving on

---

## Code Statistics

**Total Lines**: ~2,000 lines across all files
- Header: `sddl2_vm.h` (~400 lines)
- Implementation: `sddl2_vm.c` (~450 lines)
- Tests: ~1,150 lines across 4 test files

**Test Coverage**: 70 tests
- Phase 1 (Foundation): 7 tests
- Phase 2 (Arithmetic): 25 tests
- Phase 3 (Input Buffer): 18 tests
- Phase 4 (Segments): 20 tests

---

## Key Insight

**The VM's value is in segment generation**. Everything else supports that goal.

- ✅ Arithmetic - Compute segment sizes dynamically
- ✅ Input buffer - Read data to determine segment boundaries
- ✅ Stack - Manage intermediate values
- ✅ Segments - **The actual output!**

**Focus**: We achieved the core goal in Phase 4. Future phases add refinement (tags, types, validation).

---

**Last Updated**: 2025-11-06
**Next Milestone**: Phase 5 (Tag Registry) when needed
