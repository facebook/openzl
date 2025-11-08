// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * OpenZL Execution Engine - VM Internal Structures
 *
 * This header defines the internal runtime structures for the OpenZL VM,
 * as specified in the OpenZL Execution Engine Specification v0.2.
 *
 * The VM is a stack-based execution engine that:
 * - Traverses input buffers exactly once
 * - Defines tagged segments over byte ranges
 * - Automatically chunks segments
 * - Converts segments into typed streams
 */

#ifndef SDDL2_VM_H
#define SDDL2_VM_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* ============================================================================
 * Value System (Section 10)
 * ========================================================================= */

/**
 * Value kinds supported by the VM stack.
 * The VM stack operates on three distinct value kinds:
 * - I64: 64-bit signed integer values
 * - Tag: Segment tag identifiers
 * - Type: Type descriptors for segments
 */
typedef enum {
    SDDL2_VALUE_I64  = 1,
    SDDL2_VALUE_TAG  = 2,
    SDDL2_VALUE_TYPE = 3,
} SDDL2_value_kind;

/**
 * Type descriptor structure.
 * Represents the type of a segment, including:
 * - kind: The primitive type (U8, I16LE, Float32BE, etc.)
 * - width: Number of elements (1 for scalar, >1 for arrays/fixed-size types)
 *
 * Total byte size = openzl_type_size(kind) * width
 */
typedef enum {
    SDDL2_TYPE_U8 = 0,
    SDDL2_TYPE_I8,
    SDDL2_TYPE_U16LE,
    SDDL2_TYPE_U16BE,
    SDDL2_TYPE_I16LE,
    SDDL2_TYPE_I16BE,
    SDDL2_TYPE_U32LE,
    SDDL2_TYPE_U32BE,
    SDDL2_TYPE_I32LE,
    SDDL2_TYPE_I32BE,
    SDDL2_TYPE_U64LE,
    SDDL2_TYPE_U64BE,
    SDDL2_TYPE_I64LE,
    SDDL2_TYPE_I64BE,
    SDDL2_TYPE_F32LE,
    SDDL2_TYPE_F32BE,
    SDDL2_TYPE_F64LE,
    SDDL2_TYPE_F64BE,
    SDDL2_TYPE_BYTES,        // Raw bytes, no interpretation
    /* SDDL2_TYPE_FIXED_N */ // TODO: Fixed-width byte arrays
} SDDL2_type_kind;

typedef struct {
    SDDL2_type_kind kind;
    uint32_t width; // Size in number of elements
} SDDL2_type;

/**
 * Tagged value on the VM stack.
 * This is a discriminated union representing one of three value kinds.
 */
typedef struct {
    SDDL2_value_kind kind;
    union {
        int64_t as_i64;     // For SDDL2_VALUE_I64
        uint32_t as_tag;    // For SDDL2_VALUE_TAG
        SDDL2_type as_type; // For SDDL2_VALUE_TYPE
    } value;
} SDDL2_value;

/* ============================================================================
 * Stack Structure (Section 10)
 * ========================================================================= */

/**
 * Maximum configurable stack depth.
 * Cannot be overridden.
 */
#define OPENZL_STACK_DEPTH_MAX 512384

/**
 * Default maximum stack depth.
 * This can be overridden when creating a stack via openzl_stack_create().
 */
#define OPENZL_STACK_DEPTH_DEFAULT 4096

/**
 * VM stack structure.
 * LIFO stack with configurable maximum depth.
 * Stack items are allocated via arena allocation.
 */
typedef struct {
    SDDL2_value* items; // Pointer to stack items (arena-allocated)
    size_t top;         // Index of next free slot (0 = empty stack)
    size_t capacity;    // Maximum stack depth
} SDDL2_stack;

/**
 * VM error codes.
 * Used as return values for all VM operations.
 */
typedef enum {
    SDDL2_OK = 0,
    SDDL2_STACK_OVERFLOW,  // Stack capacity exceeded
    SDDL2_STACK_UNDERFLOW, // Pop from empty stack
    SDDL2_TYPE_MISMATCH,   // Operation received wrong value type
    SDDL2_LOAD_BOUNDS,     // Load address out of bounds
    SDDL2_SEGMENT_BOUNDS,  // Segment extends beyond input buffer
    // Future: SDDL2_DIV_ZERO, etc.
} SDDL2_error;

/* ============================================================================
 * Segments (Phase 4-5)
 * ========================================================================= */

/**
 * Segment structure with tag and type.
 * Represents a typed, tagged region of input data.
 */
typedef struct {
    uint32_t tag;      // Segment identifier (0 = unspecified)
    size_t start_pos;  // Start offset in input buffer
    size_t size_bytes; // Length in bytes
    SDDL2_type type; // Element type (defines array of type.kind with type.width
                     // elements)
} SDDL2_segment;

/**
 * Dynamic list of segments.
 * Grows as segments are created during VM execution.
 */
typedef struct {
    SDDL2_segment* items; // Dynamic array of segments
    size_t count;         // Number of segments
    size_t capacity;      // Allocated capacity
} SDDL2_segment_list;

/**
 * Tag registry for tracking tag usage (Phase 5).
 * Tags are registered on first use to ensure consistency.
 */
typedef struct {
    uint32_t* tags;  // Array of registered tag IDs
    size_t count;    // Number of registered tags
    size_t capacity; // Allocated capacity
} SDDL2_tag_registry;

/* ============================================================================
 * Input Buffer (Phase 3)
 * ========================================================================= */

/**
 * Input buffer structure for reading data.
 *
 * Note: Naming: this feels awkward,
 * I think the object manages a cursor into a read-only buffer,
 * which is different and should be reflected in the name.
 *
 * Lifetime: The caller owns `data` and must ensure it outlives this buffer.
 * The VM never modifies or frees the data pointer.
 *
 * The VM traverses the input buffer exactly once, creating segments.
 */
typedef struct {
    const void* data;   // Borrowed pointer to input data (any type)
    size_t size;        // Total size in bytes
    size_t current_pos; // Cursor for sequential segment creation
} SDDL2_input_buffer;

/* ============================================================================
 * Stack Operations
 * ========================================================================= */

/**
 * Initialize an empty stack.
 */
void SDDL2_stack_init(SDDL2_stack* stack);

/**
 * Push a value onto the stack.
 * Returns SDDL2_STACK_OVERFLOW if stack is full.
 *
 * NOTE: Kept as inline for performance - this is on the hot path,
 * called for every VM instruction that produces a value.
 */
static inline SDDL2_error SDDL2_stack_push(
        SDDL2_stack* stack,
        SDDL2_value value)
{
    if (stack->top >= stack->capacity) {
        return SDDL2_STACK_OVERFLOW;
    }
    stack->items[stack->top++] = value;
    return SDDL2_OK;
}

/**
 * Pop a value from the stack.
 * Returns SDDL2_STACK_UNDERFLOW if stack is empty.
 *
 * NOTE: Kept as inline for performance - this is on the hot path,
 * called for every VM instruction that consumes a value.
 */
static inline SDDL2_error SDDL2_stack_pop(SDDL2_stack* stack, SDDL2_value* out)
{
    if (stack->top == 0) {
        return SDDL2_STACK_UNDERFLOW;
    }
    *out = stack->items[--stack->top];
    return SDDL2_OK;
}

/**
 * Peek at the top value without removing it.
 * Returns SDDL2_STACK_UNDERFLOW if stack is empty.
 */
SDDL2_error SDDL2_stack_peek(const SDDL2_stack* stack, SDDL2_value* out);

/**
 * Get current stack depth.
 */
size_t SDDL2_stack_depth(const SDDL2_stack* stack);

/**
 * Check if stack is empty.
 */
int SDDL2_stack_is_empty(const SDDL2_stack* stack);

/* ============================================================================
 * Value Construction Helpers
 * ========================================================================= */

/**
 * Create an I64 value.
 */
static inline SDDL2_value SDDL2_value_i64(int64_t val)
{
    SDDL2_value v;
    v.kind         = SDDL2_VALUE_I64;
    v.value.as_i64 = val;
    return v;
}

/**
 * Create a Tag value.
 */
static inline SDDL2_value SDDL2_value_tag(uint32_t tag_id)
{
    SDDL2_value v;
    v.kind         = SDDL2_VALUE_TAG;
    v.value.as_tag = tag_id;
    return v;
}

/**
 * Create a Type value.
 */
static inline SDDL2_value SDDL2_value_type(SDDL2_type type)
{
    SDDL2_value v;
    v.kind          = SDDL2_VALUE_TYPE;
    v.value.as_type = type;
    return v;
}

/* ============================================================================
 * Type Utilities
 * ========================================================================= */

/**
 * Get the size in bytes of a single element of the given type.
 * Returns 1 for BYTES (raw bytes with no known interpretation).
 * Returns 0 for unknown/invalid types.
 */
size_t SDDL2_type_size(SDDL2_type_kind kind);

/* ============================================================================
 * Arithmetic Operations (Phase 2)
 * ========================================================================= */

/**
 * Add two I64 values from the stack.
 * Stack: a:I64 b:I64 -> (a+b):I64
 * Errors: TypeMismatch, Overflow
 */
SDDL2_error SDDL2_op_add(SDDL2_stack* stack);

/**
 * Subtract two I64 values from the stack.
 * Stack: a:I64 b:I64 -> (a-b):I64
 * Errors: TypeMismatch, Overflow
 */
SDDL2_error SDDL2_op_sub(SDDL2_stack* stack);

/**
 * Multiply two I64 values from the stack.
 * Stack: a:I64 b:I64 -> (a*b):I64
 * Errors: TypeMismatch, Overflow
 */
SDDL2_error SDDL2_op_mul(SDDL2_stack* stack);

/**
 * Divide two I64 values from the stack.
 * Stack: a:I64 b:I64 -> (a/b):I64
 * Errors: TypeMismatch, DivZero
 */
SDDL2_error SDDL2_op_div(SDDL2_stack* stack);

/**
 * Modulo of two I64 values from the stack.
 * Stack: a:I64 b:I64 -> (a%b):I64
 * Errors: TypeMismatch, DivZero
 */
SDDL2_error SDDL2_op_mod(SDDL2_stack* stack);

/**
 * Absolute value of I64 value from the stack.
 * Stack: a:I64 -> abs(a):I64
 * Errors: TypeMismatch, Overflow (on INT64_MIN)
 */
SDDL2_error SDDL2_op_abs(SDDL2_stack* stack);

/**
 * Negate I64 value from the stack.
 * Stack: a:I64 -> (-a):I64
 * Errors: TypeMismatch, Overflow (on INT64_MIN)
 */
SDDL2_error SDDL2_op_neg(SDDL2_stack* stack);

/* ============================================================================
 * Input Buffer Operations (Phase 3)
 * ========================================================================= */

/**
 * Initialize an input buffer.
 */
void SDDL2_input_buffer_init(
        SDDL2_input_buffer* buffer,
        const void* data,
        size_t size);

/**
 * Push current cursor position onto stack.
 * Stack: (empty) -> current_pos:I64
 * Does NOT advance cursor.
 */
SDDL2_error SDDL2_op_current_pos(
        SDDL2_stack* stack,
        const SDDL2_input_buffer* buffer);

/**
 * Load unsigned byte at address.
 * Stack: addr:I64 -> value:I64
 * Errors: TypeMismatch, LoadBounds
 * Does NOT advance cursor.
 */
SDDL2_error SDDL2_op_load_u8(
        SDDL2_stack* stack,
        const SDDL2_input_buffer* buffer);

/* ============================================================================
 * Segment Operations (Phase 4-5)
 * ========================================================================= */

/**
 * Initialize a segment list.
 */
void SDDL2_segment_list_init(SDDL2_segment_list* list);

/**
 * Free segment list resources.
 * Does NOT free the list structure itself (caller owns it).
 */
void SDDL2_segment_list_destroy(SDDL2_segment_list* list);

/**
 * Create an unspecified segment (no tag, no type, just bytes).
 * Stack: size:I64 -> (nothing)
 * Side effects:
 *   - Advances buffer->current_pos by size
 *   - Appends segment to list with tag=0
 * Errors: TypeMismatch, StackUnderflow, SegmentBounds
 */
SDDL2_error SDDL2_op_segment_create_unspecified(
        SDDL2_stack* stack,
        SDDL2_input_buffer* buffer,
        SDDL2_segment_list* segments);

/* ============================================================================
 * Tag Registry Operations (Phase 5)
 * ========================================================================= */

/**
 * Initialize a tag registry.
 */
void SDDL2_tag_registry_init(SDDL2_tag_registry* registry);

/**
 * Free tag registry resources.
 * Does NOT free the registry structure itself (caller owns it).
 */
void SDDL2_tag_registry_destroy(SDDL2_tag_registry* registry);

/**
 * Create a typed, tagged segment with automatic merging.
 * Stack: tag:Tag type:Type size:I64 -> (nothing)
 *
 * Parameter order rationale:
 *   - tag: Identifies WHICH logical entity (e.g., "user_ids", "timestamps")
 *   - type: Describes WHAT data structure (e.g., I32LE array, U8 bytes)
 *   - size: Quantifies HOW MUCH data (number of elements in the array)
 *   Tag + Type together define the segment's identity, then size quantifies it.
 *
 * The type defines the unit type of the array. For example:
 *   - type={U8, width=1}: Array of bytes
 *   - type={I32LE, width=1}: Array of little-endian 32-bit integers
 *   - type={F64BE, width=1}: Array of big-endian 64-bit floats
 *
 * The actual byte size of the segment is calculated as:
 *   size_bytes = element_count * SDDL2_type_size(type.kind)
 *
 * Automatic Merging Behavior:
 *   If the last segment has the same tag AND same type AND is consecutive
 *   (previous.start_pos + previous.size_bytes == new.start_pos),
 *   the new segment will be merged into the existing one by
 *   increasing its size_bytes instead of creating a new segment.
 *
 * Example:
 *   tag.const 100
 *   type.const {U8, 1}
 *   push 100              // 100 elements of U8 = 100 bytes
 *   segment_create_tagged  // seg[0]: {tag=100, start=0, size=100, type=U8}
 *
 *   tag.const 100
 *   type.const {U8, 1}
 *   push 50
 *   segment_create_tagged  // Merged! seg[0]: {tag=100, start=0, size=150,
 * type=U8}
 *
 *   tag.const 200
 *   type.const {I32LE, 1}
 *   push 20
 *   segment_create_tagged  // seg[1]: {tag=200, start=150, size=20, type=I32LE}
 *
 * Side effects:
 *   - Advances buffer->current_pos by size
 *   - Either merges with last segment OR appends new segment
 *   - Registers tag on first use
 *
 * Errors: TypeMismatch, StackUnderflow, SegmentBounds
 */
SDDL2_error SDDL2_op_segment_create_tagged(
        SDDL2_stack* stack,
        SDDL2_input_buffer* buffer,
        SDDL2_segment_list* segments,
        SDDL2_tag_registry* registry);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // SDDL2_VM_H
