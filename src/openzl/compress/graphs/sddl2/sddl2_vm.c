// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * OpenZL Execution Engine - VM Implementation
 *
 * Implementation of non-performance-critical VM functions.
 * Performance-critical functions (push/pop) remain inlined in the header.
 */

#include "sddl2_vm.h"
#include <stdbool.h>
#include "openzl/common/logging.h"
#include "openzl/shared/mem.h"      // ZL_memcpy() for memory operations
#include "openzl/shared/overflow.h" // ZL_overflowMulU32, ZL_overflowAddST, etc.

/* ============================================================================
 * Stack Operations
 *
 * Provides basic stack management functions for the SDDL2 VM.
 * Performance-critical push/pop operations are inlined in the header.
 * These functions handle: initialization, peek, depth queries, and emptiness
 * checks.
 * ========================================================================= */

void SDDL2_Stack_init(SDDL2_Stack* stack)
{
    stack->top = 0;
}

SDDL2_Error SDDL2_Stack_peek(const SDDL2_Stack* stack, SDDL2_Value* out)
{
    if (stack->top == 0) {
        return SDDL2_STACK_UNDERFLOW;
    }
    *out = stack->items[stack->top - 1];
    return SDDL2_OK;
}

size_t SDDL2_Stack_depth(const SDDL2_Stack* stack)
{
    return stack->top;
}

int SDDL2_Stack_is_empty(const SDDL2_Stack* stack)
{
    return stack->top == 0;
}

/* ============================================================================
 * Memory Allocation Fallback Implementations
 * ========================================================================= */

#ifdef SDDL2_ENABLE_TEST_ALLOCATOR

/* Test mode: Real stdlib allocator fallbacks for when alloc_fn is NULL */
#    include <stdlib.h>

void* sddl2_fallback_realloc(void* ptr, size_t size)
{
    return realloc(ptr, size);
}

void sddl2_fallback_free(void* ptr)
{
    free(ptr);
}

#else

/* Production mode: No-op/failing stubs (no stdlib dependency) */
void* sddl2_fallback_realloc(void* ptr, size_t size)
{
    (void)ptr;
    (void)size;
    return NULL; // Always fail - production must provide allocator
}

void sddl2_fallback_free(void* ptr)
{
    (void)ptr;
    // No-op - production never uses heap allocation
}

#endif // SDDL2_ENABLE_TEST_ALLOCATOR

/* ============================================================================
 * Type Utilities
 *
 * Helper functions for calculating sizes of SDDL2 types.
 * - SDDL2_kind_size(): Returns byte size for a given type kind (U8, I16LE,
 * etc.)
 * - SDDL2_Type_size(): Returns total size accounting for width multiplier
 * Handles both primitive types and complex structures.
 * ========================================================================= */

size_t SDDL2_kind_size(SDDL2_Type_kind kind)
{
    switch (kind) {
        case SDDL2_TYPE_U8:
        case SDDL2_TYPE_I8:
        case SDDL2_TYPE_F8:
            return 1;
        case SDDL2_TYPE_U16LE:
        case SDDL2_TYPE_U16BE:
        case SDDL2_TYPE_I16LE:
        case SDDL2_TYPE_I16BE:
        case SDDL2_TYPE_F16LE:
        case SDDL2_TYPE_F16BE:
        case SDDL2_TYPE_BF16LE:
        case SDDL2_TYPE_BF16BE:
            return 2;
        case SDDL2_TYPE_U32LE:
        case SDDL2_TYPE_U32BE:
        case SDDL2_TYPE_I32LE:
        case SDDL2_TYPE_I32BE:
        case SDDL2_TYPE_F32LE:
        case SDDL2_TYPE_F32BE:
            return 4;
        case SDDL2_TYPE_U64LE:
        case SDDL2_TYPE_U64BE:
        case SDDL2_TYPE_I64LE:
        case SDDL2_TYPE_I64BE:
        case SDDL2_TYPE_F64LE:
        case SDDL2_TYPE_F64BE:
            return 8;
        case SDDL2_TYPE_BYTES:
            return 1; // Raw bytes, unit size is 1 byte
        case SDDL2_TYPE_STRUCTURE:
            return 0; // Structures don't have a fixed kind size (use
                      // struct_data)
        default:
            return 0; // Unknown type
    }
}

size_t SDDL2_Type_size(SDDL2_Type type)
{
    // Handle structures specially
    if (type.kind == SDDL2_TYPE_STRUCTURE) {
        assert(type.struct_data != NULL);
        if (type.struct_data == NULL) {
            // Should not happen
            return 0;
        }
        return type.struct_data->total_size_bytes * type.width;
    }

    // For primitives, use kind size
    size_t kind_size = SDDL2_kind_size(type.kind);
    assert(kind_size > 0);
    if (kind_size == 0) {
        // Unknown type kind: should not happen
        return 0;
    }
    return kind_size * type.width;
}

/* ============================================================================
 * Memory Management Abstraction Layer (Forward Declarations)
 * ========================================================================= */

static void* sddl2_realloc(
        void* old_ptr,
        size_t old_size,
        size_t new_size,
        SDDL2_allocator_fn alloc_fn,
        void* alloc_ctx);

static void sddl2_free(void* ptr, SDDL2_allocator_fn alloc_fn);

/* ============================================================================
 * Value Construction Helpers
 *
 * Simple constructors for SDDL2_Value types.
 * Used by Generic Stack Helpers (push_i64) and throughout VM operations.
 * ========================================================================= */

SDDL2_Value SDDL2_Value_i64(int64_t val)
{
    SDDL2_Value v;
    v.kind         = SDDL2_VALUE_I64;
    v.value.as_i64 = val;
    return v;
}

SDDL2_Value SDDL2_Value_tag(uint32_t tag_id)
{
    SDDL2_Value v;
    v.kind         = SDDL2_VALUE_TAG;
    v.value.as_tag = tag_id;
    return v;
}

SDDL2_Value SDDL2_Value_type(SDDL2_Type type)
{
    SDDL2_Value v;
    v.kind          = SDDL2_VALUE_TYPE;
    v.value.as_type = type;
    return v;
}

/* ============================================================================
 * Generic Stack Operation Helpers
 *
 * Internal helper functions that encapsulate common stack operation patterns:
 * - pop_i64(): Pop and validate I64 value
 * - pop_positive_i64(): Pop and validate positive I64, convert to size_t
 * - pop_binary_i64(): Pop two I64 values for binary operations
 * - pop_tag(), pop_type(): Type-specific pop operations
 * - push_i64(): Push I64 result
 * These reduce boilerplate and ensure consistent type checking.
 * ========================================================================= */

/**
 * Pop a single I64 value from stack with type checking.
 * Common pattern for unary operations and address calculations.
 */
static inline SDDL2_Error pop_i64(SDDL2_Stack* stack, int64_t* out)
{
    SDDL2_Value val;
    SDDL2_TRY(SDDL2_Stack_pop(stack, &val));

    if (val.kind != SDDL2_VALUE_I64) {
        return SDDL2_TYPE_MISMATCH;
    }

    *out = val.value.as_i64;
    return SDDL2_OK;
}

/**
 * Pop a non-negative I64 value from stack and convert to size_t.
 * Common pattern for count/size operations (>= 0).
 * Used by type.fixed_array and type.structure for element/member counts.
 */
static inline SDDL2_Error pop_positive_i64(SDDL2_Stack* stack, size_t* out)
{
    SDDL2_Value val;
    SDDL2_TRY(SDDL2_Stack_pop(stack, &val));

    if (val.kind != SDDL2_VALUE_I64) {
        return SDDL2_TYPE_MISMATCH;
    }

    if (val.value.as_i64 < 0) {
        return SDDL2_TYPE_MISMATCH;
    }

    *out = (size_t)val.value.as_i64;
    return SDDL2_OK;
}

/**
 * Pop two I64 values from stack with type checking (b first, then a).
 * Common pattern for binary arithmetic operations.
 * Stack order: ... a b [top] → pops b, then a
 */
static inline SDDL2_Error
pop_binary_i64(SDDL2_Stack* stack, int64_t* a_out, int64_t* b_out)
{
    int64_t b, a;

    // Pop in reverse order: b (top), then a
    SDDL2_TRY(pop_i64(stack, &b));
    SDDL2_TRY(pop_i64(stack, &a));

    *a_out = a;
    *b_out = b;
    return SDDL2_OK;
}

/**
 * Pop a Tag value from stack with type checking.
 */
static inline SDDL2_Error pop_tag(SDDL2_Stack* stack, uint32_t* out)
{
    SDDL2_Value val;
    SDDL2_TRY(SDDL2_Stack_pop(stack, &val));

    if (val.kind != SDDL2_VALUE_TAG) {
        return SDDL2_TYPE_MISMATCH;
    }

    *out = val.value.as_tag;
    return SDDL2_OK;
}

/**
 * Pop a Type value from stack with type checking.
 */
static inline SDDL2_Error pop_type(SDDL2_Stack* stack, SDDL2_Type* out)
{
    SDDL2_Value val;
    SDDL2_TRY(SDDL2_Stack_pop(stack, &val));

    if (val.kind != SDDL2_VALUE_TYPE) {
        return SDDL2_TYPE_MISMATCH;
    }

    *out = val.value.as_type;
    return SDDL2_OK;
}

/**
 * Push an I64 result to stack.
 * Common pattern for operations that produce integer results.
 */
static inline SDDL2_Error push_i64(SDDL2_Stack* stack, int64_t value)
{
    return SDDL2_Stack_push(stack, SDDL2_Value_i64(value));
}

/* ============================================================================
 * Type Operations
 *
 * Implements type construction operations for creating complex types:
 * - type.fixed_array: Creates array type by multiplying base type width
 * - type.structure: Creates structure type from member types
 * Both operations include overflow detection and proper memory management.
 * ========================================================================= */

SDDL2_Error SDDL2_op_type_fixed_array(SDDL2_Stack* stack)
{
    // Pop array count (must be positive)
    size_t array_count;
    SDDL2_TRY(pop_positive_i64(stack, &array_count));

    // Pop the base type from stack
    SDDL2_Type base_type;
    SDDL2_TRY(pop_type(stack, &base_type));

    // Create new type with multiplied width
    SDDL2_Type array_type = base_type;
    if (ZL_overflowMulU32(
                base_type.width, (uint32_t)array_count, &array_type.width)) {
        ZL_DLOG(ERROR,
                "Width multiplication would overflow: base_width=%u, array_count=%zu",
                base_type.width,
                array_count);
        return SDDL2_MATH_OVERFLOW;
    }

    // Push the array type back onto stack
    return SDDL2_Stack_push(stack, SDDL2_Value_type(array_type));
}

SDDL2_Error SDDL2_op_type_structure(
        SDDL2_Stack* stack,
        SDDL2_allocator_fn alloc_fn,
        void* alloc_ctx)
{
    // Pop member count (must be positive)
    size_t member_count;
    SDDL2_TRY(pop_positive_i64(stack, &member_count));

    // Calculate allocation size for structure data
    // size = sizeof(header) + member_count * sizeof(SDDL2_Type)
    size_t allocation_size =
            sizeof(SDDL2_Struct_data) + member_count * sizeof(SDDL2_Type);

    // Allocate structure data
    if (alloc_fn == NULL) {
        assert(alloc_ctx == NULL);
        alloc_fn = sddl2_fallback_realloc; // mostly for tests without arena
    }
    SDDL2_Struct_data* const struct_data = alloc_fn(alloc_ctx, allocation_size);

    if (struct_data == NULL) {
        return SDDL2_ALLOCATION_FAILED;
    }

    // Initialize structure metadata
    struct_data->member_count     = member_count;
    struct_data->total_size_bytes = 0; // Will compute below

    // Pop member types from stack (in reverse order since stack is LIFO)
    // Stack has: Type₀ Type₁ Type₂ ... Typeₙ₋₁ [top was count]
    // We need to pop Typeₙ₋₁ first, then Typeₙ₋₂, etc.
    // To get them in order, we pop into array in reverse
    for (size_t i = 0; i < member_count; i++) {
        size_t index = member_count - 1 - i; // Reverse index

        SDDL2_Error err = pop_type(stack, &struct_data->members[index]);
        if (err != SDDL2_OK) {
            // When *not* in arena mode (i.e. during tests),
            // it's necessary to free() here to avoid memory leaks
            sddl2_free(struct_data, alloc_fn);
            return err;
        }
    }

    // Compute total size by summing all member sizes
    for (size_t i = 0; i < member_count; i++) {
        size_t member_size = SDDL2_Type_size(struct_data->members[i]);

        // Check for size overflow when adding member_size
        if (ZL_overflowAddST(
                    struct_data->total_size_bytes,
                    member_size,
                    &struct_data->total_size_bytes)) {
            sddl2_free(struct_data, alloc_fn);
            return SDDL2_MATH_OVERFLOW;
        }
    }

    // Create structure type
    SDDL2_Type struct_type = { .kind  = SDDL2_TYPE_STRUCTURE,
                               .width = 1, // Single instance (can be multiplied
                                           // later with type.fixed_array)
                               .struct_data = struct_data };

    // Push structure type onto stack
    return SDDL2_Stack_push(stack, SDDL2_Value_type(struct_type));
}

SDDL2_Error SDDL2_op_type_sizeof(SDDL2_Stack* stack)
{
    // Pop the type
    SDDL2_Type type;
    SDDL2_TRY(pop_type(stack, &type));

    // Get the size of the type
    size_t size = SDDL2_Type_size(type);

    // Push the size as I64
    return push_i64(stack, (int64_t)size);
}

/* ============================================================================
 * Arithmetic Operations
 *
 * Implements basic arithmetic operations (add, sub, mul, div, mod, abs, neg).
 * All operations include overflow detection and return SDDL2_STACK_OVERFLOW
 * on overflow. Division operations check for divide-by-zero.
 * ========================================================================= */

/**
 * Helper: Check if addition would overflow.
 * Example: add_would_overflow(INT64_MAX, 1) → true
 *          add_would_overflow(100, 200) → false
 */
static inline bool add_would_overflow(int64_t a, int64_t b)
{
    if (b > 0 && a > INT64_MAX - b)
        return true;
    if (b < 0 && a < INT64_MIN - b)
        return true;
    return false;
}

/**
 * Helper: Check if subtraction would overflow.
 * Uses: (b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)
 */
static inline bool sub_would_overflow(int64_t a, int64_t b)
{
    if (b < 0 && a > INT64_MAX + b)
        return true;
    if (b > 0 && a < INT64_MIN + b)
        return true;
    return false;
}

/**
 * Helper: Check if multiplication would overflow.
 */
static inline bool mul_would_overflow(int64_t a, int64_t b)
{
    // Special cases
    if (a == 0 || b == 0)
        return false;
    if (a == 1 || b == 1)
        return false;
    if (a == -1)
        return (b == INT64_MIN);
    if (b == -1)
        return (a == INT64_MIN);

    // Check if a * b would overflow
    if (a > 0) {
        if (b > 0) {
            return a > INT64_MAX / b;
        } else {
            return b < INT64_MIN / a;
        }
    } else {
        if (b > 0) {
            return a < INT64_MIN / b;
        } else {
            return a < INT64_MAX / b;
        }
    }
}

SDDL2_Error
SDDL2_op_add(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    (void)trace;
    (void)pc;

    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));

    if (add_would_overflow(a, b)) {
        return SDDL2_MATH_OVERFLOW;
    }

    return push_i64(stack, a + b);
}

SDDL2_Error
SDDL2_op_sub(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    (void)trace;
    (void)pc;

    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));

    if (sub_would_overflow(a, b)) {
        return SDDL2_MATH_OVERFLOW;
    }

    return push_i64(stack, a - b);
}

SDDL2_Error
SDDL2_op_mul(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    (void)trace;
    (void)pc;

    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));

    if (mul_would_overflow(a, b)) {
        return SDDL2_MATH_OVERFLOW;
    }

    return push_i64(stack, a * b);
}

SDDL2_Error
SDDL2_op_div(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    (void)trace;
    (void)pc;

    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));

    // Divide by zero check
    if (b == 0) {
        return SDDL2_DIV_ZERO;
    }

    // Overflow check: INT64_MIN / -1 = overflow
    if (a == INT64_MIN && b == -1) {
        return SDDL2_MATH_OVERFLOW;
    }

    return push_i64(stack, a / b);
}

SDDL2_Error
SDDL2_op_mod(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    (void)trace;
    (void)pc;

    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));

    // Divide by zero check
    if (b == 0) {
        return SDDL2_DIV_ZERO;
    }

    return push_i64(stack, a % b);
}

SDDL2_Error
SDDL2_op_abs(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    (void)trace;
    (void)pc;

    int64_t a;
    SDDL2_TRY(pop_i64(stack, &a));

    // Check for INT64_MIN (abs(INT64_MIN) overflows)
    if (a == INT64_MIN) {
        return SDDL2_MATH_OVERFLOW;
    }

    return push_i64(stack, (a < 0) ? -a : a);
}

SDDL2_Error
SDDL2_op_neg(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    (void)trace;
    (void)pc;

    int64_t a;
    SDDL2_TRY(pop_i64(stack, &a));

    // Check for INT64_MIN (negation overflows)
    if (a == INT64_MIN) {
        return SDDL2_MATH_OVERFLOW;
    }

    return push_i64(stack, -a);
}

/* ============================================================================
 * Comparison Operations (CMP Family)
 *
 * Implements comparison operations that return 0 (false) or 1 (true):
 * - eq, ne: Equality/inequality checks
 * - lt, le, gt, ge: Relational comparisons
 * All operations work on signed I64 values.
 * ========================================================================= */

static void SDDL2_log_binary_op(
        const char* op_name,
        const char* op_symbol,
        int64_t a,
        int64_t b,
        int64_t result,
        SDDL2_Trace_buffer* trace,
        size_t pc);

static void SDDL2_log_unary_op(
        const char* op_name,
        const char* op_symbol,
        int64_t a,
        int64_t result,
        SDDL2_Trace_buffer* trace,
        size_t pc);

SDDL2_Error
SDDL2_op_eq(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));
    int64_t result = (a == b);
    SDDL2_log_binary_op("cmp.eq", "==", a, b, result, trace, pc);
    return push_i64(stack, result);
}

SDDL2_Error
SDDL2_op_ne(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));
    int64_t result = (a != b);
    SDDL2_log_binary_op("cmp.ne", "!=", a, b, result, trace, pc);
    return push_i64(stack, result);
}

SDDL2_Error
SDDL2_op_lt(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));
    int64_t result = (a < b);
    SDDL2_log_binary_op("cmp.lt", "<", a, b, result, trace, pc);
    return push_i64(stack, result);
}

SDDL2_Error
SDDL2_op_le(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));
    int64_t result = (a <= b);
    SDDL2_log_binary_op("cmp.le", "<=", a, b, result, trace, pc);
    return push_i64(stack, result);
}

SDDL2_Error
SDDL2_op_gt(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));
    int64_t result = (a > b);
    SDDL2_log_binary_op("cmp.gt", ">", a, b, result, trace, pc);
    return push_i64(stack, result);
}

SDDL2_Error
SDDL2_op_ge(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));
    int64_t result = (a >= b);
    SDDL2_log_binary_op("cmp.ge", ">=", a, b, result, trace, pc);
    return push_i64(stack, result);
}

/* ============================================================================
 * Logical Operations (LOGIC Family)
 *
 * Implements boolean/logical operations on I64 values:
 * - and: Logical AND (returns 0 or 1)
 * - or: Logical OR (returns 0 or 1)
 * - xor: Logical XOR (returns 0 or 1)
 * - not: Logical NOT (returns 0 or 1)
 * All operations treat 0 as false and non-zero as true.
 * ========================================================================= */

SDDL2_Error
SDDL2_op_and(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));
    int64_t result = (a && b);
    SDDL2_log_binary_op("logic.and", "&&", a, b, result, trace, pc);
    return push_i64(stack, result);
}

SDDL2_Error
SDDL2_op_or(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));
    int64_t result = (a || b);
    SDDL2_log_binary_op("logic.or", "||", a, b, result, trace, pc);
    return push_i64(stack, result);
}

SDDL2_Error
SDDL2_op_xor(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));
    int64_t result = ((a && !b) || (!a && b));
    SDDL2_log_binary_op("logic.xor", "^^", a, b, result, trace, pc);
    return push_i64(stack, result);
}

SDDL2_Error
SDDL2_op_not(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    int64_t a;
    SDDL2_TRY(pop_i64(stack, &a));
    int64_t result = (!a);
    SDDL2_log_unary_op("logic.not", "!", a, result, trace, pc);
    return push_i64(stack, result);
}

/* ============================================================================
 * Stack Manipulation Operations (STACK Family)
 *
 * Provides basic stack manipulation primitives:
 * - drop: Remove top value from stack
 * - dup: Duplicate top value
 * - swap: Exchange top two values
 * These are type-agnostic and work with any stack value.
 * ========================================================================= */

SDDL2_Error
SDDL2_op_drop(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    (void)trace;
    (void)pc;

    SDDL2_Value val;
    return SDDL2_Stack_pop(stack, &val);
}

SDDL2_Error
SDDL2_op_stack_drop_if(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    (void)trace;
    (void)pc;

    int64_t condition;
    SDDL2_TRY(pop_i64(stack, &condition));

    if (condition != 0) {
        SDDL2_Value val;
        return SDDL2_Stack_pop(stack, &val);
    }

    return SDDL2_OK;
}

SDDL2_Error
SDDL2_op_dup(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    (void)trace;
    (void)pc;

    SDDL2_Value val;
    SDDL2_TRY(SDDL2_Stack_peek(stack, &val));
    return SDDL2_Stack_push(stack, val);
}

SDDL2_Error
SDDL2_op_swap(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace, size_t pc)
{
    (void)trace;
    (void)pc;

    if (stack->top < 2) {
        return SDDL2_STACK_UNDERFLOW;
    }

    SDDL2_Value temp             = stack->items[stack->top - 1];
    stack->items[stack->top - 1] = stack->items[stack->top - 2];
    stack->items[stack->top - 2] = temp;

    return SDDL2_OK;
}

/* ============================================================================
 * Validation Operations (EXPECT Family)
 *
 * Provides runtime validation and assertion operations:
 * - expect_true: Verify that a condition (I64 value) is non-zero
 * These enable data validation and contract checking in SDDL2 programs.
 * Combined with comparison and logic operations, they can express complex
 * validation rules (e.g., cmp.eq + expect_true validates equality).
 * ========================================================================= */

static void SDDL2_log_expect_true_failure(
        const SDDL2_Trace_buffer* trace,
        const SDDL2_Stack* stack);

SDDL2_Error SDDL2_op_expect_true(SDDL2_Stack* stack, SDDL2_Trace_buffer* trace)
{
    int64_t value;
    SDDL2_TRY(pop_i64(stack, &value));

    if (value == 0) {
        SDDL2_log_expect_true_failure(trace, stack);

        // Reset trace buffer (stop and clear) - NULL-safe
        SDDL2_Trace_buffer_reset(trace);

        return SDDL2_VALIDATION_FAILED;
    }

    // Success - reset trace buffer (stop and clear) - NULL-safe
    SDDL2_Trace_buffer_reset(trace);

    return SDDL2_OK;
}

/* ============================================================================
 * Input Cursor Operations
 *
 * Provides operations for reading data from the input:
 * - Cursor initialization and query operations (current_pos, remaining)
 * - Load operations for various integer types (u8, i8, u16le/be, etc.)
 * All load operations include bounds checking and return SDDL2_LOAD_BOUNDS on
 * error. Supports both little-endian (le) and big-endian (be) byte orders.
 * ========================================================================= */

void SDDL2_Input_cursor_init(
        SDDL2_Input_cursor* buffer,
        const void* data,
        size_t size)
{
    buffer->data        = data;
    buffer->size        = size;
    buffer->current_pos = 0;
}

/**
 * Helper: Check bounds for load operations.
 * Validates that an address and size fit within the buffer.
 */
static inline SDDL2_Error
check_load_bounds(const SDDL2_Input_cursor* buffer, int64_t addr, size_t size)
{
    if (addr < 0 || (size_t)addr + size > buffer->size) {
        return SDDL2_LOAD_BOUNDS;
    }
    return SDDL2_OK;
}

SDDL2_Error SDDL2_op_current_pos(
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer)
{
    // Push current cursor position as I64
    return push_i64(stack, (int64_t)buffer->current_pos);
}

SDDL2_Error SDDL2_op_remaining(
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer)
{
    size_t remaining = buffer->size - buffer->current_pos;
    return push_i64(stack, (int64_t)remaining);
}

SDDL2_Error SDDL2_op_push_stack_depth(SDDL2_Stack* stack)
{
    return push_i64(stack, (int64_t)stack->top);
}

/**
 * Macro-generated load operations.
 * All 12 load operations follow identical control flow with only size and
 * read expressions differing. Using a macro ensures consistency and reduces
 * boilerplate from ~150 lines to ~40 lines.
 */
static void SDDL2_log_load(const char* op_name, int64_t addr, int64_t value);

#define DEFINE_LOAD_OP(name, size, read_expr)                     \
    SDDL2_Error SDDL2_op_load_##name(                             \
            SDDL2_Stack* stack, const SDDL2_Input_cursor* buffer) \
    {                                                             \
        int64_t addr;                                             \
        SDDL2_TRY(pop_i64(stack, &addr));                         \
        SDDL2_TRY(check_load_bounds(buffer, addr, size));         \
        const uint8_t* bytes = (const uint8_t*)buffer->data;      \
        int64_t value        = (int64_t)(read_expr);              \
        SDDL2_log_load(#name, addr, value);                       \
        return push_i64(stack, value);                            \
    }

// 8-bit loads
DEFINE_LOAD_OP(u8, 1, bytes[addr])
DEFINE_LOAD_OP(i8, 1, (int8_t)bytes[addr])

// 16-bit loads (little-endian)
DEFINE_LOAD_OP(u16le, 2, ZL_readLE16(&bytes[addr]))
DEFINE_LOAD_OP(i16le, 2, (int16_t)ZL_readLE16(&bytes[addr]))

// 16-bit loads (big-endian)
DEFINE_LOAD_OP(u16be, 2, ZL_readBE16(&bytes[addr]))
DEFINE_LOAD_OP(i16be, 2, (int16_t)ZL_readBE16(&bytes[addr]))

// 32-bit loads (little-endian)
DEFINE_LOAD_OP(u32le, 4, ZL_readLE32(&bytes[addr]))
DEFINE_LOAD_OP(i32le, 4, (int32_t)ZL_readLE32(&bytes[addr]))

// 32-bit loads (big-endian)
DEFINE_LOAD_OP(u32be, 4, ZL_readBE32(&bytes[addr]))
DEFINE_LOAD_OP(i32be, 4, (int32_t)ZL_readBE32(&bytes[addr]))

// 64-bit loads
DEFINE_LOAD_OP(i64le, 8, (int64_t)ZL_readLE64(&bytes[addr]))
DEFINE_LOAD_OP(i64be, 8, (int64_t)ZL_readBE64(&bytes[addr]))

#undef DEFINE_LOAD_OP

/* ============================================================================
 * Segment Operations
 *
 * Implements operations for creating data segments in the output:
 * - segment.create.unspecified: Create byte segment without tag
 * - segment.create.tagged: Create typed segment with tag identifier
 * Segments are automatically merged when consecutive with same tag/type.
 * Uses segment_create_internal() as unified implementation.
 * ========================================================================= */

/* ============================================================================
 * Memory Management Abstraction Layer
 *
 * Provides unified memory management supporting both arena and heap allocation:
 * - ensure_capacity(): Generic dynamic array growth with 2x strategy
 * - sddl2_realloc(): Arena/heap-aware realloc abstraction
 * - sddl2_free(): Arena/heap-aware free abstraction
 * Arena mode: Allocates new memory and copies (no-op on free)
 * Heap mode: Uses standard realloc/free for testing
 * ========================================================================= */

// Forward declarations
static int tag_registry_register(
        SDDL2_Tag_registry* registry,
        uint32_t tag,
        SDDL2_Type type);

/**
 * Initial capacity for dynamic arrays when growing from zero.
 * This is primarily a fail-safe since init functions now pre-allocate capacity.
 * Set to 32 to reduce early reallocations if pre-allocation fails.
 */
#define SDDL2_DYNAMIC_ARRAY_INITIAL_CAPACITY 32

/**
 * Generic dynamic array capacity growth helper.
 * Implements 2x growth strategy with configurable limits.
 *
 * @param items_ptr_addr Address of items array pointer (as void*) - will be
 * updated on success
 * @param count Current item count
 * @param capacity_ptr Pointer to current capacity (will be updated on success)
 * @param element_size Size of each element in bytes
 * @param max_capacity Maximum allowed capacity
 * @param alloc_fn Allocator function
 * @param alloc_ctx Allocator context
 * @return 1 on success, 0 on failure (max capacity reached or allocation
 * failed)
 */
static int ensure_capacity(
        void* items_ptr_addr,
        size_t count,
        size_t* capacity_ptr,
        size_t element_size,
        size_t max_capacity,
        SDDL2_allocator_fn alloc_fn,
        void* alloc_ctx)
{
    void** items_ptr = (void**)items_ptr_addr;

    // Already have capacity
    if (count < *capacity_ptr) {
        return 1;
    }

    // Check against maximum capacity limit
    if (*capacity_ptr >= max_capacity) {
        return 0; // Maximum capacity reached
    }

    // Calculate new capacity: 2x growth
    size_t new_capacity = (*capacity_ptr == 0)
            ? SDDL2_DYNAMIC_ARRAY_INITIAL_CAPACITY
            : (*capacity_ptr * 2);

    // Cap at maximum capacity
    if (new_capacity > max_capacity) {
        new_capacity = max_capacity;
    }

    // Reallocate
    size_t old_size = count * element_size;
    size_t new_size = new_capacity * element_size;

    void* new_items =
            sddl2_realloc(*items_ptr, old_size, new_size, alloc_fn, alloc_ctx);

    if (!new_items) {
        return 0; // Allocation failed
    }

    // Update pointers
    *items_ptr    = new_items;
    *capacity_ptr = new_capacity;
    return 1;
}

/**
 * Unified realloc-like abstraction supporting both arena and heap allocation.
 *
 * @param old_ptr Existing allocation (NULL for initial allocation)
 * @param old_size Size of old allocation in bytes (used for copying)
 * @param new_size Desired new size in bytes
 * @param alloc_fn Allocator function (NULL = use fallback)
 * @param alloc_ctx Allocator context (e.g., ZL_Graph* for arena allocation)
 * @return New allocation, or NULL on failure
 */
static void* sddl2_realloc(
        void* old_ptr,
        size_t old_size,
        size_t new_size,
        SDDL2_allocator_fn alloc_fn,
        void* alloc_ctx)
{
    if (alloc_fn != NULL) {
        // Arena path: allocate new + copy old data
        void* new_ptr = alloc_fn(alloc_ctx, new_size);
        if (new_ptr == NULL) {
            return NULL; // Allocation failed
        }

        // Copy old data if it exists
        assert(new_size >= old_size);
        if (old_ptr != NULL && old_size > 0) {
            ZL_memcpy(new_ptr, old_ptr, old_size);
        }

        return new_ptr;
    } else {
        // Fallback: real realloc (test mode) or NULL (production mode)
        return sddl2_fallback_realloc(old_ptr, new_size);
    }
}

/**
 * Unified free abstraction supporting both arena and heap allocation.
 *
 * @param ptr Pointer to free (can be NULL)
 * @param alloc_fn Allocator function (NULL = use fallback)
 */
static void sddl2_free(void* ptr, SDDL2_allocator_fn alloc_fn)
{
    if (alloc_fn == NULL) {
        sddl2_fallback_free(ptr);
    }
    // Arena-allocated memory: no-op (arena handles cleanup)
}

/* ============================================================================
 * Segment Registry Operations
 *
 * Manages the list of all created segments:
 * - Stores segments in creation order
 * - Supports dynamic growth with pre-allocation for arena allocators
 * - Automatically merges consecutive segments with same tag/type
 * - Each segment tracks: tag, start position, size, and type information
 * Used during SDDL2 execution to build the segment table.
 * ========================================================================= */

void SDDL2_Segment_list_init(
        SDDL2_Segment_list* list,
        SDDL2_allocator_fn alloc_fn,
        void* alloc_ctx)
{
    list->items     = NULL;
    list->count     = 0;
    list->capacity  = 0;
    list->alloc_fn  = alloc_fn;
    list->alloc_ctx = alloc_ctx;

    // Pre-allocate initial capacity for arena allocators
    if (alloc_fn != NULL) {
        size_t initial_size =

                SDDL2_SEGMENT_INITIAL_CAPACITY * sizeof(SDDL2_Segment);
        list->items = (SDDL2_Segment*)alloc_fn(alloc_ctx, initial_size);
        if (list->items != NULL) {
            list->capacity = SDDL2_SEGMENT_INITIAL_CAPACITY;
        }
        // If allocation fails, capacity remains 0 and will be handled
        // by segment_list_ensure_capacity() when first segment is added
    }
}

void SDDL2_Segment_list_destroy(SDDL2_Segment_list* list)
{
    sddl2_free(list->items, list->alloc_fn);
    list->items    = NULL;
    list->count    = 0;
    list->capacity = 0;
}

/**
 * Helper: Ensure segment list has capacity for at least one more item.
 * Grows by 2x when needed.
 *
 * Uses the unified sddl2_realloc() abstraction which handles both
 * arena allocation and heap allocation transparently.
 *
 * Returns 0 if capacity limit is exceeded or allocation fails.
 */
static int segment_list_ensure_capacity(SDDL2_Segment_list* list)
{
    return ensure_capacity(
            (void*)&list->items,
            list->count,
            &list->capacity,
            sizeof(SDDL2_Segment),
            SDDL2_SEGMENT_MAX_CAPACITY,
            list->alloc_fn,
            list->alloc_ctx);
}

/**
 * Internal helper: Create a segment with tag, type, and element count.
 * Handles validation, merging, and cursor advancement.
 *
 * This is the unified implementation for both tagged and unspecified segments.
 * An unspecified segment is just a tagged segment with tag=0 and type=BYTES.
 *
 * @param tag Segment tag (0 for unspecified)
 * @param type Segment type descriptor
 * @param element_count Number of elements (size is element_count * type_size)
 * @param buffer Input buffer (cursor will be advanced)
 * @param segments Segment list (segment will be appended or merged)
 * @param registry Tag registry (tag will be registered if non-zero)
 * @return SDDL2_OK on success, error code on failure
 */
static SDDL2_Error segment_create_internal(
        uint32_t tag,
        SDDL2_Type type,
        size_t element_count,
        SDDL2_Input_cursor* buffer,
        SDDL2_Segment_list* segments,
        SDDL2_Tag_registry* registry)
{
    // Calculate actual size in bytes
    // total_type_size = size of one instance of the type (including width)
    // segment_size = element_count × total_type_size
    size_t total_type_size = SDDL2_Type_size(type);
    if (total_type_size == 0) {
        return SDDL2_TYPE_MISMATCH; // Unknown or invalid type
    }

    // Check for overflow in element_count * total_type_size multiplication
    size_t size_bytes;
    if (ZL_overflowMulST(element_count, total_type_size, &size_bytes)) {
        return SDDL2_MATH_OVERFLOW; // Size overflow
    }

    // Bounds check: segment must fit in remaining input
    if (buffer->current_pos + size_bytes > buffer->size) {
        return SDDL2_SEGMENT_BOUNDS;
    }

    // Register tag if non-zero (tagged segments only)
    if (tag != 0) {
        if (!tag_registry_register(registry, tag, type)) {
            return SDDL2_TYPE_MISMATCH; // Tag already registered with different
                                        // type, or capacity limit exceeded, or
                                        // allocation failed
        }
    }

    // Check if we can merge with the last segment
    // Merge conditions: same tag AND same type AND consecutive positions
    // This applies to ALL segments, including unspecified ones (tag=0)
    // Unspecified segments merge to reduce overhead for "leftover" data
    if (segments->count > 0) {
        SDDL2_Segment* last = &segments->items[segments->count - 1];
        size_t expected_pos = last->start_pos + last->size_bytes;

        if (last->tag == tag && expected_pos == buffer->current_pos) {
            // If tags match, types MUST match due to tag-type uniqueness:
            // - Non-zero tags: enforced by tag_registry_register() above
            // - Tag 0 (unspecified): always BYTES type by definition
            assert(last->type.kind == type.kind);
            assert(last->type.width == type.width);
            assert(type.kind != SDDL2_TYPE_STRUCTURE
                   || last->type.struct_data == type.struct_data);

            // MERGE: Just extend the last segment's size
            last->size_bytes += size_bytes;
            buffer->current_pos += size_bytes;
            return SDDL2_OK;
        }
    }

    // Cannot merge - create new segment
    if (!segment_list_ensure_capacity(segments)) {
        return SDDL2_LIMIT_EXCEEDED; // Capacity limit exceeded or allocation
                                     // failed
    }

    SDDL2_Segment seg;
    seg.tag        = tag;
    seg.start_pos  = buffer->current_pos;
    seg.size_bytes = size_bytes;
    seg.type       = type;

    segments->items[segments->count++] = seg;

    // Advance cursor
    buffer->current_pos += size_bytes;

    return SDDL2_OK;
}

SDDL2_Error SDDL2_op_segment_create_unspecified(
        SDDL2_Stack* stack,
        SDDL2_Input_cursor* buffer,
        SDDL2_Segment_list* segments)
{
    // Pop size from stack
    int64_t size_i64;
    SDDL2_TRY(pop_i64(stack, &size_i64));

    // Validate size (must be non-negative)
    if (size_i64 < 0) {
        return SDDL2_TYPE_MISMATCH;
    }

    // Unspecified segment = tag 0, type BYTES
    SDDL2_Type bytes_type = { .kind = SDDL2_TYPE_BYTES, .width = 1 };

    // Delegate to internal helper (registry can be NULL since tag=0)
    return segment_create_internal(
            0, bytes_type, (size_t)size_i64, buffer, segments, NULL);
}

/* ============================================================================
 * Tag Registry Operations
 *
 * Manages the set of unique non-zero tags used in segments:
 * - Tracks all unique tags in creation order
 * - Prevents duplicate tag registration
 * - Supports dynamic growth with pre-allocation for arena allocators
 * - Used by tagged segment creation to ensure tag uniqueness
 * Tags serve as identifiers to reference specific data regions.
 * ========================================================================= */

void SDDL2_Tag_registry_init(
        SDDL2_Tag_registry* registry,
        SDDL2_allocator_fn alloc_fn,
        void* alloc_ctx)
{
    registry->entries   = NULL;
    registry->count     = 0;
    registry->capacity  = 0;
    registry->alloc_fn  = alloc_fn;
    registry->alloc_ctx = alloc_ctx;

    // Pre-allocate initial capacity for arena allocators
    if (alloc_fn != NULL) {
        size_t initial_size =
                SDDL2_TAG_INITIAL_CAPACITY * sizeof(SDDL2_Tag_entry);
        registry->entries = (SDDL2_Tag_entry*)alloc_fn(alloc_ctx, initial_size);
        if (registry->entries != NULL) {
            registry->capacity = SDDL2_TAG_INITIAL_CAPACITY;
        }
        // If allocation fails, capacity remains 0 and will be handled
        // by tag_registry_register() when first tag is registered
    }
}

void SDDL2_Tag_registry_destroy(SDDL2_Tag_registry* registry)
{
    sddl2_free(registry->entries, registry->alloc_fn);
    registry->entries  = NULL;
    registry->count    = 0;
    registry->capacity = 0;
}

/**
 * Helper: Compare two types for equality.
 * Returns true if types match (kind, width, and struct_data for structures).
 */
static bool types_equal(SDDL2_Type a, SDDL2_Type b)
{
    if (a.kind != b.kind || a.width != b.width) {
        return false;
    }

    // For structures, also compare struct_data pointers
    if (a.kind == SDDL2_TYPE_STRUCTURE) {
        return a.struct_data == b.struct_data;
    }

    return true;
}

/**
 * Helper: Register a tag with its associated type.
 * If tag already exists, validates that the type matches.
 * Returns 1 on success, 0 on allocation failure or type mismatch.
 *
 * Semantic constraint: A tag uniquely identifies a type.
 * Attempting to use the same tag with different types is an error.
 */
static int tag_registry_register(
        SDDL2_Tag_registry* registry,
        uint32_t tag,
        SDDL2_Type type)
{
    // Check if tag is already registered
    for (size_t i = 0; i < registry->count; i++) {
        if (registry->entries[i].tag == tag) {
            // Tag exists - verify type matches
            if (!types_equal(registry->entries[i].type, type)) {
                // Type mismatch! Same tag used with different types
                ZL_DLOG(ERROR,
                        "Tag %u already registered with different type "
                        "(existing kind=%d width=%u, new kind=%d width=%u)",
                        tag,
                        registry->entries[i].type.kind,
                        registry->entries[i].type.width,
                        type.kind,
                        type.width);
                return 0; // Type mismatch error
            }
            return 1; // Already registered with same type - OK
        }
    }

    // Tag not yet registered - add it
    if (!ensure_capacity(
                (void*)&registry->entries,
                registry->count,
                &registry->capacity,
                sizeof(SDDL2_Tag_entry),
                SDDL2_TAG_MAX_CAPACITY,
                registry->alloc_fn,
                registry->alloc_ctx)) {
        return 0; // Allocation failed or capacity limit reached
    }

    // Register tag with type
    registry->entries[registry->count].tag  = tag;
    registry->entries[registry->count].type = type;
    registry->count++;
    return 1;
}

/* ============================================================================
 * Trace Buffer Operations
 *
 * Trace buffer lifecycle and manipulation functions for validation debugging.
 * Used to collect operation traces during execution and dump them on
 * expect_true failure.
 * ========================================================================= */

void SDDL2_Trace_buffer_init(
        SDDL2_Trace_buffer* trace,
        SDDL2_allocator_fn alloc_fn,
        void* alloc_ctx)
{
    trace->entries   = NULL;
    trace->count     = 0;
    trace->capacity  = 0;
    trace->active    = 0;
    trace->alloc_fn  = alloc_fn;
    trace->alloc_ctx = alloc_ctx;
}

void SDDL2_Trace_buffer_destroy(SDDL2_Trace_buffer* trace)
{
    sddl2_free(trace->entries, trace->alloc_fn);
    trace->entries  = NULL;
    trace->count    = 0;
    trace->capacity = 0;
    trace->active   = 0;
}

/**
 * Start trace collection.
 * Activates the trace buffer to begin recording operations.
 * NULL-safe: Does nothing if trace is NULL.
 */
void SDDL2_Trace_buffer_start(SDDL2_Trace_buffer* trace)
{
    if (!trace)
        return;
    trace->active = 1;
}

/**
 * Stop trace collection without clearing the buffer.
 * Deactivates trace collection but preserves recorded entries.
 * NULL-safe: Does nothing if trace is NULL.
 */
void SDDL2_Trace_buffer_stop(SDDL2_Trace_buffer* trace)
{
    if (!trace)
        return;
    trace->active = 0;
}

/**
 * Reset the trace buffer (stop and clear).
 * Stops trace collection and clears all recorded entries.
 * NULL-safe: Does nothing if trace is NULL.
 */
void SDDL2_Trace_buffer_reset(SDDL2_Trace_buffer* trace)
{
    if (!trace)
        return;
    trace->active = 0;
    trace->count  = 0;
}

/**
 * Append a trace entry to the buffer.
 * Only records if tracing is active.
 * Returns 1 on success, 0 on allocation failure.
 */
int SDDL2_Trace_buffer_append(
        SDDL2_Trace_buffer* trace,
        size_t pc,
        const char* op_name,
        const char* details)
{
    // Only append if tracing is active
    if (!trace->active) {
        return 1; // Success (no-op when inactive)
    }

    // Ensure capacity for new entry
    if (!ensure_capacity(
                (void*)&trace->entries,
                trace->count,
                &trace->capacity,
                sizeof(SDDL2_Trace_entry),
                SDDL2_TRACE_MAX_CAPACITY,
                trace->alloc_fn,
                trace->alloc_ctx)) {
        return 0; // Allocation failed or capacity exceeded
    }

    // Create and append the trace entry
    SDDL2_Trace_entry* entry = &trace->entries[trace->count++];
    entry->pc                = pc;
    entry->op_name           = op_name;

    // Copy details string (safely truncate if needed)
    size_t details_len = 0;
    if (details) {
        while (details[details_len]
               && details_len < SDDL2_TRACE_DETAILS_SIZE - 1) {
            entry->details[details_len] = details[details_len];
            details_len++;
        }
    }
    entry->details[details_len] = '\0';

    return 1;
}

/**
 * Dump the trace buffer to ERROR log.
 * Used when expect_true fails to show the execution context.
 */
void SDDL2_Trace_buffer_dump(const SDDL2_Trace_buffer* trace)
{
    if (trace->count == 0) {
        ZL_DLOG(ERROR, "[ERROR] No trace entries recorded");
        return;
    }

    ZL_DLOG(ERROR, "[ERROR] Execution trace (%zu entries):", trace->count);
    for (size_t i = 0; i < trace->count; i++) {
        const SDDL2_Trace_entry* entry = &trace->entries[i];
        if (entry->details[0] != '\0') {
            ZL_DLOG(ERROR,
                    "[ERROR]   PC=%zu: %s - %s",
                    entry->pc,
                    entry->op_name,
                    entry->details);
        } else {
            ZL_DLOG(ERROR, "[ERROR]   PC=%zu: %s", entry->pc, entry->op_name);
        }
    }
}

SDDL2_Error SDDL2_op_segment_create_tagged(
        SDDL2_Stack* stack,
        SDDL2_Input_cursor* buffer,
        SDDL2_Segment_list* segments,
        SDDL2_Tag_registry* registry)
{
    // Pop size, type, and tag from stack (size on top, type middle, tag bottom)
    int64_t size_i64;
    SDDL2_Type type;
    uint32_t tag;

    // Pop in reverse order: size (top), type, tag (bottom)
    SDDL2_TRY(pop_i64(stack, &size_i64));
    SDDL2_TRY(pop_type(stack, &type));
    SDDL2_TRY(pop_tag(stack, &tag));

    // Validate size (must be non-negative)
    if (size_i64 < 0) {
        return SDDL2_TYPE_MISMATCH;
    }

    // Delegate to internal helper
    return segment_create_internal(
            tag, type, (size_t)size_i64, buffer, segments, registry);
}

/* ============================================================================
 * Trace/Diagnostic Functions
 *
 * Helper functions for debugging and diagnostic output.
 * Placed at end of file to minimize visual clutter in main operation code.
 * ========================================================================= */

/**
 * Log binary operation details for debugging and trace recording.
 *
 * Unified logging function for CMP and LOGIC binary operations.
 * Outputs operands, operator, and result at POS log level for fine-grained
 * tracing during bytecode execution. Also records in trace buffer if active.
 *
 * @param op_name Full operation name including family (e.g., "cmp.eq",
 * "logic.and")
 * @param op_symbol Symbolic operator (e.g., "==", "&")
 * @param a First operand
 * @param b Second operand
 * @param result Operation result
 * @param trace Trace buffer for recording (NULL-safe)
 * @param pc Program counter for trace entry
 */
static void SDDL2_log_binary_op(
        const char* op_name,
        const char* op_symbol,
        int64_t a,
        int64_t b,
        int64_t result,
        SDDL2_Trace_buffer* trace,
        size_t pc)
{
    ZL_DLOG(POS,
            "[SDDL2] %s: %lld %s %lld → %lld",
            op_name,
            (long long)a,
            op_symbol,
            (long long)b,
            (long long)result);

    if (trace && trace->active) {
        char details[SDDL2_TRACE_DETAILS_SIZE];
        snprintf(
                details,
                sizeof(details),
                "%s: %lld %s %lld → %lld",
                op_name,
                (long long)a,
                op_symbol,
                (long long)b,
                (long long)result);
        SDDL2_Trace_buffer_append(trace, pc, op_name, details);
    }
}

/**
 * Log unary operation details for debugging and trace recording.
 *
 * Unified logging function for unary operations.
 * Outputs operand, operator, and result at POS log level for fine-grained
 * tracing during bytecode execution. Also records in trace buffer if active.
 *
 * @param op_name Full operation name including family (e.g., "logic.not")
 * @param op_symbol Symbolic operator (e.g., "~")
 * @param a Operand
 * @param result Operation result
 * @param trace Trace buffer for recording (NULL-safe)
 * @param pc Program counter for trace entry
 */
static void SDDL2_log_unary_op(
        const char* op_name,
        const char* op_symbol,
        int64_t a,
        int64_t result,
        SDDL2_Trace_buffer* trace,
        size_t pc)
{
    ZL_DLOG(POS,
            "[SDDL2] %s: %s%lld → %lld",
            op_name,
            op_symbol,
            (long long)a,
            (long long)result);

    if (trace && trace->active) {
        char details[SDDL2_TRACE_DETAILS_SIZE];
        snprintf(
                details,
                sizeof(details),
                "%s: %s%lld → %lld",
                op_name,
                op_symbol,
                (long long)a,
                (long long)result);
        SDDL2_Trace_buffer_append(trace, pc, op_name, details);
    }
}

/**
 * Log load operation details for debugging.
 *
 * Outputs address and loaded value at POS log level for fine-grained
 * tracing of memory load operations during bytecode execution.
 *
 * @param op_name Operation name (e.g., "u8", "i16le", "u32be")
 * @param addr Memory address being loaded from
 * @param value Value that was loaded
 */
static void SDDL2_log_load(const char* op_name, int64_t addr, int64_t value)
{
    ZL_DLOG(POS,
            "[SDDL2] load.%s: addr=0x%llx → %lld (0x%llx)",
            op_name,
            (unsigned long long)addr,
            (long long)value,
            (unsigned long long)value);
}

/**
 * Log concise expect_true failure with trace context and stack state.
 *
 * Dumps execution trace (if available), reports validation failure,
 * and shows remaining stack state for context.
 *
 * @param trace Trace buffer with execution history (NULL-safe, can be inactive)
 * @param stack Stack after popping the failed value (for context)
 */
static void SDDL2_log_expect_true_failure(
        const SDDL2_Trace_buffer* trace,
        const SDDL2_Stack* stack)
{
    // Dump trace if available and non-empty
    if (trace && trace->count > 0) {
        SDDL2_Trace_buffer_dump(trace);
    }

    // Concise failure message
    ZL_DLOG(ERROR,
            "[SDDL2] expect_true VALIDATION FAILURE: got 0 (expected non-zero)");

    // Show stack state if non-empty (useful for debugging context)
    if (stack->top > 0) {
        ZL_DLOG(ERROR, "[SDDL2] Remaining stack: depth=%zu", stack->top);
        size_t show_count = stack->top < 3 ? stack->top : 3;
        for (size_t i = 0; i < show_count; i++) {
            size_t idx             = stack->top - 1 - i;
            const SDDL2_Value* val = &stack->items[idx];
            switch (val->kind) {
                case SDDL2_VALUE_I64:
                    ZL_DLOG(ERROR,
                            "[SDDL2]   [%zu] I64: %lld",
                            idx,
                            (long long)val->value.as_i64);
                    break;
                case SDDL2_VALUE_TAG:
                    ZL_DLOG(ERROR,
                            "[SDDL2]   [%zu] TAG: %u",
                            idx,
                            val->value.as_tag);
                    break;
                case SDDL2_VALUE_TYPE:
                    ZL_DLOG(ERROR,
                            "[SDDL2]   [%zu] TYPE: kind=%d width=%u",
                            idx,
                            val->value.as_type.kind,
                            val->value.as_type.width);
                    break;
            }
        }
        if (stack->top > 3) {
            ZL_DLOG(ERROR, "[SDDL2]   ... and %zu more", stack->top - 3);
        }
    }
}
