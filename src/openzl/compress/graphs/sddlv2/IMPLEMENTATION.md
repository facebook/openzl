# OpenZL Execution Engine — Implementation Plan

> **Status**: Phase 1 Complete ✅  
> **Spec Version**: v0.2  
> **Last Updated**: 2025-11-06

---

## Overview

This document outlines the incremental implementation plan for the OpenZL Execution Engine VM, a stack-based runtime for executing compiled SDDL plans. The VM is designed to:

- Execute a **compiled plan** derived from SDDL
- Traverse an input buffer **exactly once**
- Define **tagged segments** over byte ranges
- Automatically **chunk** segments
- Convert segments into **typed streams**
- Send streams to a downstream **Graph** (e.g., compression)

The VM **does not** understand SDDL or data semantics beyond what is encoded into the compiled plan.

---

## Design Principles

### 1. **Incremental Development**
Build the VM in small, testable phases. Each phase adds one layer of functionality while remaining fully tested.

### 2. **Performance Where It Matters**
- Hot-path functions (push/pop) are `static inline` for zero overhead
- Cold-path functions live in `.c` files for maintainability
- Type checking and validation happen once, not per-operation

### 3. **OpenZL Naming Conventions**
- **All non-static functions**: `SDDL2_*` prefix (including `static inline` in headers)
- **Internal types/enums**: `openzl_*` prefix
- **File names**: `sddl2_*` prefix

### 4. **Arena Allocation**
- Stack and metadata allocated via arena (no per-operation frees)
- Configurable stack depth (default: 4096, max: 512384)
- Entire arena reclaimed at end of execution

### 5. **Minimal Spec**
VM is integer-only, no branching, no floats. Keeps implementation simple and predictable.

---

## Implementation Phases

### ✅ Phase 1: Foundation (Stack + Values) — **COMPLETE**

**Goal**: Establish the core runtime substrate that all VM operations build upon.

**Implemented**:
- Value system (`I64`, `Tag`, `Type` value kinds)
- Type system (19 primitive types: U8, I16LE, F32BE, BYTES, etc.)
- Stack operations (push/pop/peek with bounds checking)
- Value constructors
- Type size calculations

**Files**:
- `sddl2_vm.h` - VM interface (230 lines)
- `sddl2_vm.c` - VM implementation (80 lines)
- `sddl2_vm_test.c` - Unit tests (180 lines, 7 tests, all passing ✅)

**Key Design**:
- Stack is arena-allocated (pointer + capacity, not embedded array)
- Push/pop are `static inline` for performance (hot path)
- Type `width` field = element count (not byte count)
- `OPENZL_TYPE_BYTES` has size 1 (unit size, unknown interpretation)

**Deliverable**: Can create values, push/pop from stack, check types and sizes.

---

### Phase 2: Basic Arithmetic

**Goal**: Implement integer arithmetic operations on I64 values.

**Operations** (Spec Section 11):
- `add` - Addition with overflow detection
- `sub` - Subtraction with overflow detection
- `mul` - Multiplication with overflow detection
- `div` - Division with divide-by-zero check
- `mod` - Modulo with divide-by-zero check
- `abs` - Absolute value (fatal on `INT64_MIN`)
- `neg` - Negation (already in opcodes.def)

**Stack Contract** (RPN style):
```
a:I64  b:I64
  ── add ──>  (a+b):I64
```

**Error Handling**:
- `fatal(TypeMismatch)` if operands not I64
- `fatal(Overflow)` on arithmetic overflow
- `fatal(DivZero)` on division/modulo by zero

**Testing**:
- Basic operations (5 + 3 = 8)
- Overflow detection (INT64_MAX + 1)
- Divide by zero
- Type checking (reject non-I64 operands)

**Deliverable**: Can execute simple arithmetic programs like `push 5, push 3, add, halt`.

---

### Phase 3: Comparisons & Validation

**Goal**: Enable conditional logic and runtime assertions.

**Comparison Operations** (Spec Section 19):
- `eq` - Equality (produces I64 0 or 1)
- `ne` - Inequality
- `lt` - Less than (signed)
- `le` - Less than or equal
- `gt` - Greater than (signed)
- `ge` - Greater than or equal

**Stack Contract**:
```
a:I64  b:I64
  ── eq ──>  result:I64   (0 = false, 1 = true)
```

**Validation** (Spec Section 19.2):
- `expect_true` - Assert condition is true (fatal if 0)

**Error Handling**:
- `fatal(TypeMismatch)` if operands not I64
- `fatal(DataMismatch)` if expectation violated

**Testing**:
- All comparison operations
- Truth value semantics (0 = false, non-zero = true)
- `expect_true` success and failure cases

**Deliverable**: Can validate computed values, enabling data-dependent assertions.

---

### Phase 4: Input Buffer State

**Goal**: Enable reading data from input buffer at computed addresses.

**Input Buffer Structure** (Spec Section 2):
```c
typedef struct {
    const uint8_t* data;
    size_t file_size;
    size_t current_pos;  // Next byte to consume
} openzl_input_buffer;
```

**Operations**:
- `current_pos` - Push current cursor position (does NOT advance cursor) (Spec Section 17)

**Integer Loads** (Spec Sections 16, 18):
- Unsigned: `load.u8`, `load.u16le`, `load.u16be`, `load.u32le`, `load.u32be`
- Signed: `load.i8`, `load.i16le`, `load.i16be`, `load.i32le`, `load.i32be`, `load.i64le`, `load.i64be`

**Stack Contract**:
```
addr:I64
  ── load.i32le ──>  value:I64
```

**Bounds Checking** (Spec Section 16.2):
```
0 ≤ addr AND addr + size ≤ file_size
else fatal(Bounds)
```

**Key Design**:
- Loads **never** modify `current_pos`
- All loads produce I64 (sign/zero-extended)
- Address arithmetic uses existing `add/sub` operations

**Testing**:
- Load all integer types (various endianness)
- Bounds checking (reject out-of-range)
- Sign/zero extension correctness
- Address computation with `current_pos`

**Deliverable**: Can load integers from input buffer at computed addresses.

---

### Phase 5: Stack Manipulation

**Goal**: Flexible stack rearrangement for complex operations.

**Basic Stack Ops** (already in opcodes.def):
- `stack.dup` - Duplicate top value
- `stack.drop` - Remove top value
- `stack.swap` - Swap top two values
- `stack.over` - Copy second item to top
- `stack.rot` - Rotate top three items

**Conditional Drop** (Spec Section 11B.1):
- `drop_if` - Conditionally remove value beneath boolean predicate

**Stack Contract** for `drop_if`:
```
v  cond:I64
  ── drop_if ──>  (nothing if cond==0, else v remains)
```

**Use Case**: Optional fields
```
type.const Float32LE
...compute predicate (0/1)...
drop_if  # Keep type only if predicate is true
```

**Testing**:
- All stack manipulation operations
- Conditional drop with true/false conditions
- Polymorphic value handling (works with I64, Tag, Type)

**Deliverable**: Can manipulate stack flexibly for complex computations.

---

### Phase 6: Type System (Type Table)

**Goal**: Runtime type registry for segment type descriptors.

**Type Table**:
```c
typedef struct {
    openzl_type* types;
    size_t count;
} openzl_type_table;
```

**Operations**:
- `type.const <index>` - Push type from table (Spec Section 10A)

**Integration**:
- Type table populated at VM initialization
- Index bounds checking
- Type validation

**Testing**:
- Type table construction
- Index bounds checking
- Type retrieval

**Deliverable**: Can reference types from compiled type table.

---

### Phase 7: Segments (Simple)

**Goal**: Create tagged segments over input buffer ranges.

**Segment Structure** (Spec Section 3):
```c
typedef struct {
    uint32_t tag;
    size_t size_bytes;
    openzl_type type;
} openzl_segment;
```

**Tag Registry** (Spec Section 12):
```c
// registry[tag] → Type
// Enforces: tag reuse must have matching type
```

**Operations** (Spec Section 13):
- `segment_create` - Create simple segment

**Stack Contract**:
```
Tag  I64(sizeBytes)  Type
  ── segment_create ──>  (nothing, segment recorded)
```

**Runtime Checks**:
- Bounds: `current_pos + size_bytes ≤ file_size`
- Tag conflict: Reused tag must match registered type
- Coalescing: Adjacent segments with same tag/type merge

**Side Effects**:
- Advances `current_pos += size_bytes`
- Adds segment to pending list
- Updates tag registry

**Testing**:
- Simple segment creation
- Bounds checking
- Tag registry enforcement
- Segment coalescing

**Deliverable**: Can create simple segments over byte ranges.

---

### Phase 8: Arrays

**Goal**: Support array segments with element type checking.

**Operations** (Spec Sections 4, 5):
- `array_segment_create` - Array of single element type
- `soa_segment_create` - Array of Structures (AoS)

**Array Validation**:
```
size_bytes % elem_size == 0
else fatal(SizeMismatch)
```

**AoS Structure**:
```
Stack: Tag  I64(sizeBytes)  Type₀  Type₁  ...  Typeₙ₋₁  N
  ── soa_segment_create ──>
```

**Compute**:
```
struct_size = Σ field_size(i)
elem_count  = size_bytes / struct_size (must be integer)
```

**Testing**:
- Array size validation
- Element type checking
- Multi-field struct arrays
- Size mismatch detection

**Deliverable**: Can create array and AoS segments.

---

### Phase 9: Chunking

**Goal**: Automatic segment chunking based on size limits.

**Chunk State** (Spec Section 6):
```c
typedef struct {
    openzl_segment* pending;
    size_t pending_count;
    size_t pending_bytes;
    size_t max_chunk_size;
} openzl_chunk_state;
```

**Operations**:
- `set_max_chunk_size` - Configure chunk size limit

**Auto-Flush Logic** (Spec Section 6):
```
Before creating segment:
  if pending_bytes + new_size > max_chunk_size:
    flush_chunk()
```

**Flush Strategies** (Spec Section 6.1):
- **Split**: All tags unique → separate streams
- **Dispatch**: Any tag repeats → group by tag

**Key Constraint**: Segments are **never split** mid-segment.

**Testing**:
- Chunk size configuration
- Auto-flush triggering
- Split vs. dispatch selection
- Edge case: segment exactly at limit

**Deliverable**: Automatic chunking of pending segments.

---

### Phase 10: Output & Logging

**Goal**: Stream output and diagnostic logging.

**Stream Output** (Spec Section 9):
```c
typedef struct {
    uint32_t tag;
    openzl_type type;
    const uint8_t* data;
    size_t size;
} openzl_stream;
```

- Push streams to output list
- Default graph: `COMPRESS_GENERIC`
- No interpretation of graph behavior

**EVENT Logging** (Spec Section 14.1):
```
EVT seq op src{line,col} pos delta chunk{pending,mcs,flushed} note?
```

**ERROR Logging** (Spec Section 14.2):
```
ERR op kind msg
src: { line, col, text }
data: { pos, remaining, total }
chunk: { pending, mcs, would_add? }
operands: [ ... ]
tag_check?: { tag, expected, got }
expect?: { rule, got }
stack?: { depth, need? }
context?: { last_n, events[] }
```

**Error Semantics**: First error only, then halt.

**Testing**:
- Stream output collection
- Event logging (optional)
- Error logging (comprehensive)
- First-error-only semantics

**Deliverable**: Streams output to graph, diagnostic logging.

---

### Phase 11: SoA Dispatch

**Goal**: Fan-out AoS segments into per-field streams.

**Operation** (Spec Section 8):
- `dispatch_soa` - Split AoS into separate streams

**Fan-Out**:
```
Input:  AoS segment with N fields
Output: N streams, one per field
        Each stream has: original tag + element-level type
```

**Ordering**: Preserved across all streams.

**Testing**:
- AoS fan-out correctness
- Field ordering preservation
- Tag/type propagation

**Deliverable**: AoS segments dispatch to per-field streams.

---

### Phase 12: Integration (Execute Loop)

**Goal**: End-to-end VM execution of compiled bytecode.

**VM Structure**:
```c
typedef struct {
    openzl_stack* stack;
    openzl_input_buffer* input;
    openzl_chunk_state* chunks;
    openzl_type_table* types;
    // ... tag registry, output list, etc.
} openzl_vm;
```

**Execute Loop**:
```c
while (ip < bytecode_end) {
    opcode = fetch();
    switch (opcode) {
        case OP_ADD: execute_add(); break;
        case OP_SEGMENT_CREATE: execute_segment_create(); break;
        // ...
    }
    if (halted || error) break;
}
```

**Operations**:
- `halt` - Terminate execution
- `flush` - Explicit chunk flush

**Testing**:
- End-to-end programs
- All opcodes exercised
- Error propagation
- Halt semantics

**Deliverable**: Fully functional VM executing compiled SDDL plans.

---

## File Structure

```
src/openzl/compress/graphs/sddlv2/
├── sddl2_vm.h                   # VM interface
├── sddl2_vm.c                   # VM implementation
├── sddl2_execute.h              # Execution loop (Phase 12)
├── sddl2_execute.c
├── openzl_opcodes.def           # Opcode definitions (X-macro)
├── sddlv2.h                     # Graph API integration
├── sddlv2.c
└── IMPLEMENTATION.md            # This file

tests/compress/graphs/sddlv2/
├── sddl2_vm_test.c              # Phase 1 tests
├── sddl2_arithmetic_test.c      # Phase 2 tests
├── sddl2_compare_test.c         # Phase 3 tests
├── sddl2_input_test.c           # Phase 4 tests
├── sddl2_stack_ops_test.c       # Phase 5 tests
├── sddl2_segments_test.c        # Phases 7-8 tests
├── sddl2_chunking_test.c        # Phase 9 tests
└── sddl2_integration_test.c     # Phase 12 tests
```

---

## Current Status

### ✅ Completed
- **Phase 1**: Foundation (Stack + Values)
  - Value system with three kinds
  - Type system with 19 primitives
  - Stack with arena allocation
  - All tests passing (7 tests)

### 🚧 In Progress
- None (Phase 1 complete, awaiting next phase)

### 📋 Planned
- Phase 2: Basic Arithmetic
- Phase 3: Comparisons & Validation
- Phase 4: Input Buffer State
- Phase 5: Stack Manipulation
- Phase 6: Type System (Type Table)
- Phase 7: Segments (Simple)
- Phase 8: Arrays
- Phase 9: Chunking
- Phase 10: Output & Logging
- Phase 11: SoA Dispatch
- Phase 12: Integration

---

## Testing Strategy

### Unit Tests (Per Phase)
- Each phase has dedicated test file
- Comprehensive coverage of new functionality
- Edge cases and error conditions
- All tests must pass before proceeding

### Integration Tests (Phase 12)
- End-to-end execution of sample programs
- All opcodes exercised in combination
- Error handling and recovery
- Performance validation

### Test Execution
```bash
# Compile and run phase tests
cd /path/to/openzl
cc -std=c11 -Wall -Wextra -Isrc \
   src/openzl/compress/graphs/sddlv2/sddl2_vm.c \
   tests/compress/graphs/sddlv2/sddl2_vm_test.c \
   -o /tmp/sddl2_vm_test && /tmp/sddl2_vm_test
```

---

## References

- **OpenZL Execution Engine Specification v0.2** - Defines exact VM behavior
- **openzl_opcodes.def** - X-macro opcode definitions
- **OpenZL Function Graph API** (`zl_graph_api.h`) - Integration interface

---

## Notes

### Memory Management
- All VM state allocated via arena (Spec Section 15)
- No per-operation frees
- Entire arena reclaimed at end of execution
- Low churn expected; arena expansion is rare

### Error Model
- First error only, then halt (Spec Section 14.2)
- Rich diagnostics on error
- No error recovery; execution terminates

### Performance Considerations
- Push/pop on hot path → `static inline`
- Type checking once per instruction, not per operation
- Bounds checking explicit and minimal
- No dynamic allocation during execution

### Opcode Families (from openzl_opcodes.def)
- PUSH (0x0001) - Constants
- MATH (0x0002) - Arithmetic
- CMP (0x0003) - Comparisons
- LOGIC (0x0004) - Logical ops
- CONTROL (0x0005) - Flow control
- LOAD (0x0006) - Memory loads
- STACK (0x0007) - Stack manipulation
- TYPE (0x0008) - Type operations
- VAR (0x0009) - Variables
- EXPECT (0x000A) - Validation
- CALL (0x000B) - Function calls

---

**Last Updated**: 2025-11-06  
**Next Milestone**: Phase 2 (Basic Arithmetic)
