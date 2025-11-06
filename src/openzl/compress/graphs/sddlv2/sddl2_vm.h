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
    OPENZL_VALUE_I64 = 1,
    OPENZL_VALUE_TAG = 2,
    OPENZL_VALUE_TYPE = 3,
} openzl_value_kind;

/**
 * Type descriptor structure.
 * Represents the type of a segment, including:
 * - kind: The primitive type (U8, I16LE, Float32BE, etc.)
 * - width: Number of elements (1 for scalar, >1 for arrays/fixed-size types)
 * 
 * Total byte size = openzl_type_size(kind) * width
 */
typedef enum {
    OPENZL_TYPE_U8 = 0,
    OPENZL_TYPE_I8,
    OPENZL_TYPE_U16LE,
    OPENZL_TYPE_U16BE,
    OPENZL_TYPE_I16LE,
    OPENZL_TYPE_I16BE,
    OPENZL_TYPE_U32LE,
    OPENZL_TYPE_U32BE,
    OPENZL_TYPE_I32LE,
    OPENZL_TYPE_I32BE,
    OPENZL_TYPE_U64LE,
    OPENZL_TYPE_U64BE,
    OPENZL_TYPE_I64LE,
    OPENZL_TYPE_I64BE,
    OPENZL_TYPE_F32LE,
    OPENZL_TYPE_F32BE,
    OPENZL_TYPE_F64LE,
    OPENZL_TYPE_F64BE,
    OPENZL_TYPE_BYTES,       // Raw bytes, no interpretation
    /* OPENZL_TYPE_FIXED_N */  // TODO: Fixed-width byte arrays
} openzl_type_kind;

typedef struct {
    openzl_type_kind kind;
    uint32_t width;  // Size in number of elements
} openzl_type;

/**
 * Tagged value on the VM stack.
 * This is a discriminated union representing one of three value kinds.
 */
typedef struct {
    openzl_value_kind kind;
    union {
        int64_t as_i64;      // For OPENZL_VALUE_I64
        uint32_t as_tag;     // For OPENZL_VALUE_TAG
        openzl_type as_type; // For OPENZL_VALUE_TYPE
    } value;
} openzl_value;

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
    openzl_value* items;  // Pointer to stack items (arena-allocated)
    size_t top;           // Index of next free slot (0 = empty stack)
    size_t capacity;      // Maximum stack depth
} openzl_stack;

/**
 * Error codes for stack operations.
 */
typedef enum {
    OPENZL_STACK_OK = 0,
    OPENZL_STACK_OVERFLOW,
    OPENZL_STACK_UNDERFLOW,
    OPENZL_STACK_TYPE_MISMATCH,
} openzl_stack_error;

/* ============================================================================
 * Stack Operations
 * ========================================================================= */

/**
 * Initialize an empty stack.
 */
void SDDL2_stack_init(openzl_stack* stack);

/**
 * Push a value onto the stack.
 * Returns OPENZL_STACK_OVERFLOW if stack is full.
 * 
 * NOTE: Kept as inline for performance - this is on the hot path,
 * called for every VM instruction that produces a value.
 */
static inline openzl_stack_error
SDDL2_stack_push(openzl_stack* stack, openzl_value value)
{
    if (stack->top >= stack->capacity) {
        return OPENZL_STACK_OVERFLOW;
    }
    stack->items[stack->top++] = value;
    return OPENZL_STACK_OK;
}

/**
 * Pop a value from the stack.
 * Returns OPENZL_STACK_UNDERFLOW if stack is empty.
 * 
 * NOTE: Kept as inline for performance - this is on the hot path,
 * called for every VM instruction that consumes a value.
 */
static inline openzl_stack_error
SDDL2_stack_pop(openzl_stack* stack, openzl_value* out)
{
    if (stack->top == 0) {
        return OPENZL_STACK_UNDERFLOW;
    }
    *out = stack->items[--stack->top];
    return OPENZL_STACK_OK;
}

/**
 * Peek at the top value without removing it.
 * Returns OPENZL_STACK_UNDERFLOW if stack is empty.
 */
openzl_stack_error
SDDL2_stack_peek(const openzl_stack* stack, openzl_value* out);

/**
 * Get current stack depth.
 */
size_t SDDL2_stack_depth(const openzl_stack* stack);

/**
 * Check if stack is empty.
 */
int SDDL2_stack_is_empty(const openzl_stack* stack);

/* ============================================================================
 * Value Construction Helpers
 * ========================================================================= */

/**
 * Create an I64 value.
 */
static inline openzl_value SDDL2_value_i64(int64_t val)
{
    openzl_value v;
    v.kind = OPENZL_VALUE_I64;
    v.value.as_i64 = val;
    return v;
}

/**
 * Create a Tag value.
 */
static inline openzl_value SDDL2_value_tag(uint32_t tag_id)
{
    openzl_value v;
    v.kind = OPENZL_VALUE_TAG;
    v.value.as_tag = tag_id;
    return v;
}

/**
 * Create a Type value.
 */
static inline openzl_value SDDL2_value_type(openzl_type type)
{
    openzl_value v;
    v.kind = OPENZL_VALUE_TYPE;
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
size_t SDDL2_type_size(openzl_type_kind kind);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // SDDL2_VM_H
