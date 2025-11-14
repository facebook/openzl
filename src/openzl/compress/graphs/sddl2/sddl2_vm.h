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
#include "openzl/compress/graphs/sddl2/sddl2_error.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* ============================================================================
 * Value System
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
} SDDL2_Value_kind;

/**
 * Type descriptor structure.
 * Represents the type of a segment, including:
 * - kind: The type category (primitive or STRUCTURE)
 * - width: Number of elements (1 for scalar, >1 for arrays)
 * - complex_data: NULL for primitives, pointer to structure data for structures
 *
 * For primitives:
 *   Total byte size = primitive_type_size(kind) * width
 *
 * For structures:
 *   Total byte size = structure_size * width
 *   (structure_size stored in complex_data)
 */
/* Primitive types: 0-23 (1, 2, 4, or 8 byte values)
 * Complex types: 100+ */
typedef enum {
    SDDL2_TYPE_BYTES = 0,
    SDDL2_TYPE_U8,
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
    SDDL2_TYPE_F8,
    SDDL2_TYPE_F16LE,
    SDDL2_TYPE_F16BE,
    SDDL2_TYPE_BF16LE,
    SDDL2_TYPE_BF16BE,
    SDDL2_TYPE_F32LE,
    SDDL2_TYPE_F32BE,
    SDDL2_TYPE_F64LE,
    SDDL2_TYPE_F64BE,

    SDDL2_TYPE_STRUCTURE = 100,
} SDDL2_Type_kind;

// Forward declaration for recursive type composition
typedef struct SDDL2_Struct_data SDDL2_Struct_data;

typedef struct {
    SDDL2_Type_kind kind; // Type category (primitive or STRUCTURE)
    uint32_t width; // Number of elements (consistent meaning across all types)
    void* complex_data; // NULL for primitives, SDDL2_Struct_data* for
                        // structures
} SDDL2_Type;

/**
 * Structure type metadata (heap-allocated).
 *
 * Contains the member types of a structure.
 * Each member is itself a full SDDL2_Type, allowing:
 * - Primitives: {U8, 1, NULL}
 * - Arrays: {I32LE, 10, NULL}
 * - Nested structures: {SDDL2_TYPE_STRUCTURE, 1, ptr_to_other_struct}
 *
 * Memory layout:
 *   Fixed header (member_count, total_size_bytes)
 *   Flexible array of member types
 */
struct SDDL2_Struct_data {
    size_t member_count;     // Number of members in the structure
    size_t total_size_bytes; // Cached: sum of all member sizes (for
                             // performance)
    SDDL2_Type members[];    // Flexible array member: the actual member types
};

/**
 * Tagged value on the VM stack.
 * This is a discriminated union representing one of three value kinds.
 */
typedef struct {
    SDDL2_Value_kind kind;
    union {
        int64_t as_i64;     // For SDDL2_VALUE_I64
        uint32_t as_tag;    // For SDDL2_VALUE_TAG
        SDDL2_Type as_type; // For SDDL2_VALUE_TYPE
    } value;
} SDDL2_Value;

/* ============================================================================
 * Stack Structure
 * ========================================================================= */

/**
 * Maximum configurable stack depth.
 * This is a hard limit and cannot be overridden.
 */
#define SDDL2_STACK_DEPTH_MAX 512384

/**
 * Default maximum stack depth.
 * Currently not used since tests provide their own stack storage.
 * Reserved for future dynamic stack allocation if needed.
 * Can be overridden at compile time with -DSDDL2_STACK_DEPTH_DEFAULT=value
 */
#ifndef SDDL2_STACK_DEPTH_DEFAULT
#    define SDDL2_STACK_DEPTH_DEFAULT 4096
#endif

/**
 * VM stack structure.
 * LIFO stack with configurable maximum depth.
 * Stack items are allocated via arena allocation.
 */
typedef struct {
    SDDL2_Value* items; // Pointer to stack items (arena-allocated)
    size_t top;         // Index of next free slot (0 = empty stack)
    size_t capacity;    // Maximum stack depth
} SDDL2_Stack;

/* ============================================================================
 * Memory Allocation Strategy
 * ========================================================================= */

/**
 * Allocator callback for arena or test allocation.
 *
 * Production: Use arena allocation (e.g., ZL_Graph_getScratchSpace)
 * Tests: Define SDDL2_ENABLE_TEST_ALLOCATOR and pass NULL for malloc fallback
 *
 * Memory is never freed individually; arena handles lifecycle in production.
 *
 * @param allocator_ctx Context (e.g., ZL_Graph* for arena)
 * @param size Bytes to allocate
 * @return Allocated memory or NULL on failure
 */
typedef void* (*SDDL2_allocator_fn)(void* allocator_ctx, size_t size);

/* ============================================================================
 * Memory Allocation Fallback Implementations
 * ========================================================================= */

#ifdef SDDL2_ENABLE_TEST_ALLOCATOR

/* Test mode: Real stdlib allocator fallbacks for when alloc_fn is NULL */
#    include <stdlib.h>

static inline void* sddl2_fallback_realloc(void* ptr, size_t size)
{
    return realloc(ptr, size);
}

static inline void sddl2_fallback_free(void* ptr)
{
    free(ptr);
}

#else

/* Production mode: No-op/failing stubs (no stdlib dependency) */
static inline void* sddl2_fallback_realloc(void* ptr, size_t size)
{
    (void)ptr;
    (void)size;
    return NULL; // Always fail - production must provide allocator
}

static inline void sddl2_fallback_free(void* ptr)
{
    (void)ptr;
    // No-op - production never uses heap allocation
}

#endif // SDDL2_ENABLE_TEST_ALLOCATOR

/**
 * Dynamic Array Capacity Configuration
 *
 * Initial capacities: Pre-allocated to reduce reallocation overhead with arena
 *                     allocators (which require new allocation + copy).
 * Maximum capacities: Hard limits to prevent unbounded memory growth.
 *                     Exceeding max results in SDDL2_LIMIT_EXCEEDED.
 *
 * All values can be overridden at compile time:
 *   -DSDDL2_SEGMENT_INITIAL_CAPACITY=value
 *   -DSDDL2_SEGMENT_MAX_CAPACITY=value
 *   -DSDDL2_TAG_INITIAL_CAPACITY=value
 *   -DSDDL2_TAG_MAX_CAPACITY=value
 */
#ifndef SDDL2_SEGMENT_INITIAL_CAPACITY
#    define SDDL2_SEGMENT_INITIAL_CAPACITY 4096
#endif
#ifndef SDDL2_SEGMENT_MAX_CAPACITY
#    define SDDL2_SEGMENT_MAX_CAPACITY 524288 // 512K segments
#endif
#ifndef SDDL2_TAG_INITIAL_CAPACITY
#    define SDDL2_TAG_INITIAL_CAPACITY 4096
#endif
#ifndef SDDL2_TAG_MAX_CAPACITY
#    define SDDL2_TAG_MAX_CAPACITY 32768 // 32K tags
#endif

/* ============================================================================
 * Segments
 * ========================================================================= */

/**
 * Segment structure with tag and type.
 * Represents a typed, tagged region of input data.
 */
typedef struct {
    uint32_t tag;      // Segment identifier (0 = unspecified)
    size_t start_pos;  // Start offset in input buffer
    size_t size_bytes; // Length in bytes
    SDDL2_Type type; // Element type (defines array of type.kind with type.width
                     // elements)
} SDDL2_Segment;

/**
 * Dynamic list of segments.
 * Grows as segments are created during VM execution.
 *
 * Uses allocator callback for memory management to remain independent
 * of OpenZL infrastructure while supporting arena allocation.
 */
typedef struct {
    SDDL2_Segment* items;        // Dynamic array of segments
    size_t count;                // Number of segments
    size_t capacity;             // Allocated capacity
    SDDL2_allocator_fn alloc_fn; // Allocator function (NULL = use realloc)
    void* alloc_ctx;             // Opaque allocator context
} SDDL2_Segment_list;

/**
 * Tag registry for tracking tag usage.
 * Tags are registered on first use to ensure consistency.
 *
 * Uses allocator callback for memory management to remain independent
 * of OpenZL infrastructure while supporting arena allocation.
 */
typedef struct {
    uint32_t* tags;              // Array of registered tag IDs
    size_t count;                // Number of registered tags
    size_t capacity;             // Allocated capacity
    SDDL2_allocator_fn alloc_fn; // Allocator function (NULL = use realloc)
    void* alloc_ctx;             // Opaque allocator context
} SDDL2_Tag_registry;

/* ============================================================================
 * Input Cursor
 * ========================================================================= */

/**
 * Input cursor structure for sequential traversal of input data.
 *
 * Tracks position within borrowed input data. The caller owns `data` and must
 * ensure it outlives this cursor. The VM never modifies or frees the data
 * pointer.
 *
 * The VM traverses the input exactly once, advancing the cursor as segments are
 * created.
 */
typedef struct {
    const void* data;   // Borrowed pointer to input data (any type)
    size_t size;        // Total size in bytes
    size_t current_pos; // Cursor for sequential segment creation
} SDDL2_Input_cursor;

/* ============================================================================
 * Stack Operations
 * ========================================================================= */

/**
 * Initialize an empty stack.
 */
void SDDL2_Stack_init(SDDL2_Stack* stack);

/**
 * Push a value onto the stack.
 * Returns SDDL2_STACK_OVERFLOW if stack is full.
 *
 * NOTE: Kept as inline for performance - this is on the hot path,
 * called for every VM instruction that produces a value.
 */
static inline SDDL2_Error SDDL2_Stack_push(
        SDDL2_Stack* stack,
        SDDL2_Value value)
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
static inline SDDL2_Error SDDL2_Stack_pop(SDDL2_Stack* stack, SDDL2_Value* out)
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
SDDL2_Error SDDL2_Stack_peek(const SDDL2_Stack* stack, SDDL2_Value* out);

/**
 * Get current stack depth.
 */
size_t SDDL2_Stack_depth(const SDDL2_Stack* stack);

/**
 * Check if stack is empty.
 */
int SDDL2_Stack_is_empty(const SDDL2_Stack* stack);

/* ============================================================================
 * Value Construction Helpers - create typed stack values
 * ========================================================================= */

static inline SDDL2_Value SDDL2_Value_i64(int64_t val)
{
    SDDL2_Value v;
    v.kind         = SDDL2_VALUE_I64;
    v.value.as_i64 = val;
    return v;
}

static inline SDDL2_Value SDDL2_Value_tag(uint32_t tag_id)
{
    SDDL2_Value v;
    v.kind         = SDDL2_VALUE_TAG;
    v.value.as_tag = tag_id;
    return v;
}

static inline SDDL2_Value SDDL2_Value_type(SDDL2_Type type)
{
    SDDL2_Value v;
    v.kind          = SDDL2_VALUE_TYPE;
    v.value.as_type = type;
    return v;
}

/* ============================================================================
 * Type Utilities
 * ========================================================================= */

/**
 * Get the size in bytes of a single element of the given type kind (primitive
 * size). Returns 1 for BYTES (raw bytes with no known interpretation). Returns
 * 0 for unknown/invalid types.
 */
size_t SDDL2_kind_size(SDDL2_Type_kind kind);

/**
 * Get the total size in bytes of a type (including width).
 * Calculates: SDDL2_kind_size(type.kind) × type.width
 * Returns 0 if type.kind is unknown.
 */
size_t SDDL2_Type_size(SDDL2_Type type);

/* ============================================================================
 * Type Operations
 * ========================================================================= */

/**
 * Create a fixed array type from base type.
 * Stack: array_count:I64 base_type:Type -> array_type:Type
 *
 * Pops an I64 array count and a Type from the stack, then pushes a new Type
 * with width multiplied by the array count. This creates a fixed-size array
 * type.
 *
 * Example:
 *   push.type.u32le        // Type{U32LE, 1}
 *   push.i32 10            // I64: 10
 *   type.fixed_array       // Type{U32LE, 10} - array of 10 U32LE elements
 *
 * @param stack The VM stack
 * @return SDDL2_OK or error code
 *
 * Errors:
 *   - SDDL2_STACK_UNDERFLOW: stack has fewer than 2 values
 *   - SDDL2_TYPE_MISMATCH: stack values are not {I64, Type}, or array_count <=
 * 0
 *   - SDDL2_STACK_OVERFLOW: width multiplication would overflow, or push would
 * overflow the stack
 */
SDDL2_Error SDDL2_op_type_fixed_array(SDDL2_Stack* stack);

/**
 * Create a structure type from member types.
 * Stack: Type₀ Type₁ Type₂ ... Typeₙ₋₁ N:I64 -> Type_struct
 *
 * Pops an I64 count N and N types from the stack, then creates a structure
 * type containing those members in order. The structure's total size is the
 * sum of all member sizes.
 *
 * Example:
 *   push.type U8           // Member 0: U8
 *   push.type I16LE        // Member 1: I16LE
 *   push.type I32LE        // Member 2: I32LE
 *   push.i64 3             // 3 members
 *   type.structure         // Type{STRUCTURE} with 7 bytes total
 *
 * Structures can contain:
 * - Primitives: {U8, 1, NULL}
 * - Arrays: {I32LE, 10, NULL}
 * - Nested structures: {SDDL2_TYPE_STRUCTURE, 1, ptr}
 *
 * @param stack The VM stack
 * @param alloc_fn Allocator function for structure data (NULL = use malloc)
 * @param alloc_ctx Allocator context (e.g., ZL_Graph* for arena allocation)
 * @return SDDL2_OK or error code
 *
 * Errors:
 *   - SDDL2_STACK_UNDERFLOW: stack has fewer than N+1 values
 *   - SDDL2_TYPE_MISMATCH: top value not I64, or any of N values not Type, or N
 * <= 0
 *   - SDDL2_ALLOCATION_FAILED: failed to allocate structure data
 *   - SDDL2_STACK_OVERFLOW: push would overflow the stack
 */
SDDL2_Error SDDL2_op_type_structure(
        SDDL2_Stack* stack,
        SDDL2_allocator_fn alloc_fn,
        void* alloc_ctx);

/* ============================================================================
 * Arithmetic Operations
 *
 * Binary operations: Stack: a:I64 b:I64 -> result:I64
 * Unary operations:  Stack: a:I64 -> result:I64
 * All check for overflow; div/mod also check for divide-by-zero.
 * Errors: TypeMismatch, Overflow (all), DivZero (div, mod only)
 * ========================================================================= */

SDDL2_Error SDDL2_op_add(SDDL2_Stack* stack); // a + b
SDDL2_Error SDDL2_op_sub(SDDL2_Stack* stack); // a - b
SDDL2_Error SDDL2_op_mul(SDDL2_Stack* stack); // a * b
SDDL2_Error SDDL2_op_div(SDDL2_Stack* stack); // a / b
SDDL2_Error SDDL2_op_mod(SDDL2_Stack* stack); // a % b
SDDL2_Error SDDL2_op_abs(SDDL2_Stack* stack); // |a|
SDDL2_Error SDDL2_op_neg(SDDL2_Stack* stack); // -a

/* ============================================================================
 * Comparison Operations (CMP Family)
 *
 * All comparison operations follow the same pattern:
 *   Stack: a:I64 b:I64 -> (comparison_result?1:0):I64
 *   Errors: TypeMismatch
 * Returns 1 if comparison is true, 0 if false.
 * ========================================================================= */

SDDL2_Error SDDL2_op_eq(SDDL2_Stack* stack); // a == b
SDDL2_Error SDDL2_op_ne(SDDL2_Stack* stack); // a != b
SDDL2_Error SDDL2_op_lt(SDDL2_Stack* stack); // a < b
SDDL2_Error SDDL2_op_le(SDDL2_Stack* stack); // a <= b
SDDL2_Error SDDL2_op_gt(SDDL2_Stack* stack); // a > b
SDDL2_Error SDDL2_op_ge(SDDL2_Stack* stack); // a >= b

/* ============================================================================
 * Logical Operations (LOGIC Family)
 *
 * Binary operations: Stack: a:I64 b:I64 -> result:I64
 * Unary operation:   Stack: a:I64 -> result:I64
 * All operations perform bitwise operations on I64 values.
 * Errors: TypeMismatch
 * ========================================================================= */

SDDL2_Error SDDL2_op_and(SDDL2_Stack* stack); // a & b
SDDL2_Error SDDL2_op_or(SDDL2_Stack* stack);  // a | b
SDDL2_Error SDDL2_op_xor(SDDL2_Stack* stack); // a ^ b
SDDL2_Error SDDL2_op_not(SDDL2_Stack* stack); // ~a

/* ============================================================================
 * Stack Manipulation Operations (STACK Family)
 * ========================================================================= */

/**
 * Drop (remove) the top value from the stack.
 * Stack: value -> (empty)
 * Errors: StackUnderflow
 */
SDDL2_Error SDDL2_op_drop(SDDL2_Stack* stack);

/**
 * Duplicate the top value on the stack.
 * Stack: value -> value value
 * Errors: StackUnderflow, StackOverflow
 */
SDDL2_Error SDDL2_op_dup(SDDL2_Stack* stack);

/**
 * Swap the top two values on the stack.
 * Stack: a b -> b a
 * Errors: StackUnderflow
 */
SDDL2_Error SDDL2_op_swap(SDDL2_Stack* stack);

/* ============================================================================
 * Validation Operations (EXPECT Family)
 * ========================================================================= */

/**
 * Validate that the top stack value is true (non-zero).
 * Stack: value:I64 -> (empty)
 *
 * Pops an I64 value from the stack and verifies it is non-zero.
 * If the value is 0 (false), returns SDDL2_VALIDATION_FAILED.
 * This enables runtime assertions and data validation in SDDL2 programs.
 *
 * Errors:
 *   - SDDL2_STACK_UNDERFLOW: stack is empty
 *   - SDDL2_TYPE_MISMATCH: top value is not I64
 *   - SDDL2_VALIDATION_FAILED: value is 0 (false)
 */
SDDL2_Error SDDL2_op_expect_true(SDDL2_Stack* stack);

/* ============================================================================
 * Input Cursor Operations
 * ========================================================================= */

/**
 * Initialize an input buffer.
 */
void SDDL2_Input_cursor_init(
        SDDL2_Input_cursor* buffer,
        const void* data,
        size_t size);

/**
 * Push current input cursor position.
 * Stack: (empty) -> current_pos:I64
 * Does NOT advance cursor.
 */
SDDL2_Error SDDL2_op_current_pos(
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer);

/**
 * Push remaining bytes in input buffer.
 * Stack: (empty) -> remaining:I64
 * Does NOT advance cursor.
 */
SDDL2_Error SDDL2_op_remaining(
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer);

/* ============================================================================
 * Load Operations
 *
 * All load operations follow the same pattern:
 *   Stack: addr:I64 -> value:I64
 *   Errors: TypeMismatch, LoadBounds
 *   Does NOT advance cursor
 *
 * Naming convention: load_[u/i][8/16/32/64][le/be]
 *   - u = unsigned (zero-extend), i = signed (sign-extend)
 *   - 8/16/32/64 = bit width
 *   - le = little-endian, be = big-endian (omitted for 8-bit)
 * ========================================================================= */

SDDL2_Error SDDL2_op_load_u8(
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer);
SDDL2_Error SDDL2_op_load_i8(
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer);
SDDL2_Error SDDL2_op_load_u16le(
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer);
SDDL2_Error SDDL2_op_load_u16be(
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer);
SDDL2_Error SDDL2_op_load_i16le(
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer);
SDDL2_Error SDDL2_op_load_i16be(
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer);
SDDL2_Error SDDL2_op_load_u32le(
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer);
SDDL2_Error SDDL2_op_load_u32be(
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer);
SDDL2_Error SDDL2_op_load_i32le(
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer);
SDDL2_Error SDDL2_op_load_i32be(
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer);
SDDL2_Error SDDL2_op_load_i64le(
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer);
SDDL2_Error SDDL2_op_load_i64be(
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer);

/* ============================================================================
 * Segment & Tag Registry Operations
 *
 * Both segment lists and tag registries follow the same lifecycle pattern:
 *   - init(): Prepares structure with optional arena allocator
 *   - destroy(): Frees resources (no-op for arena mode)
 *
 * Allocation modes:
 *   - alloc_fn=NULL: Falls back to realloc/free (test mode)
 *   - alloc_fn!=NULL: Uses arena allocation (production mode)
 *     Arena mode never frees memory individually; arena handles cleanup.
 * ========================================================================= */

void SDDL2_Segment_list_init(
        SDDL2_Segment_list* list,
        SDDL2_allocator_fn alloc_fn,
        void* alloc_ctx);

void SDDL2_Segment_list_destroy(SDDL2_Segment_list* list);

/**
 * Create an unspecified segment (no tag, no type, just bytes).
 * Stack: size:I64 -> (nothing)
 * Side effects:
 *   - Advances buffer->current_pos by size
 *   - Appends segment to list with tag=0
 * Errors: TypeMismatch, StackUnderflow, SegmentBounds
 */
SDDL2_Error SDDL2_op_segment_create_unspecified(
        SDDL2_Stack* stack,
        SDDL2_Input_cursor* buffer,
        SDDL2_Segment_list* segments);

void SDDL2_Tag_registry_init(
        SDDL2_Tag_registry* registry,
        SDDL2_allocator_fn alloc_fn,
        void* alloc_ctx);

void SDDL2_Tag_registry_destroy(SDDL2_Tag_registry* registry);

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
 *   size_bytes = element_count * SDDL2_Type_size(type.kind)
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
SDDL2_Error SDDL2_op_segment_create_tagged(
        SDDL2_Stack* stack,
        SDDL2_Input_cursor* buffer,
        SDDL2_Segment_list* segments,
        SDDL2_Tag_registry* registry);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // SDDL2_VM_H
