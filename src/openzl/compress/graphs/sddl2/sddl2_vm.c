// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * OpenZL Execution Engine - VM Implementation
 *
 * Implementation of non-performance-critical VM functions.
 * Performance-critical functions (push/pop) remain inlined in the header.
 */

#include "sddl2_vm.h"
#include <limits.h>
#include <stdbool.h>
#include "openzl/shared/mem.h" // ZL_memcpy() for memory operations

/* ============================================================================
 * Stack Operations - Non-Critical Path
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
 * Type Utilities
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
                      // complex_data)
        default:
            return 0; // Unknown type
    }
}

size_t SDDL2_Type_size(SDDL2_Type type)
{
    // Handle structures specially
    if (type.kind == SDDL2_TYPE_STRUCTURE) {
        if (type.complex_data == NULL) {
            return 0; // Invalid structure
        }
        SDDL2_Type_structure_data* struct_data =
                (SDDL2_Type_structure_data*)type.complex_data;
        return struct_data->total_size_bytes * type.width;
    }

    // For primitives, use kind size
    size_t kind_size = SDDL2_kind_size(type.kind);
    if (kind_size == 0) {
        return 0; // Unknown type kind
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
 * Type Operations
 * ========================================================================= */

SDDL2_Error SDDL2_op_type_fixed_array(SDDL2_Stack* stack)
{
    // Pop array count (I64) from stack
    SDDL2_Value array_count_val;
    SDDL2_TRY(SDDL2_Stack_pop(stack, &array_count_val));

    // Verify it's an I64 value
    if (array_count_val.kind != SDDL2_VALUE_I64) {
        return SDDL2_TYPE_MISMATCH;
    }

    int64_t array_count_i64 = array_count_val.value.as_i64;

    // Validate array_count is positive
    // Zero or negative array sizes are not supported
    if (array_count_i64 <= 0) {
        return SDDL2_TYPE_MISMATCH;
    }

    // Pop the base type from stack
    SDDL2_Value base_type_val;
    SDDL2_TRY(SDDL2_Stack_pop(stack, &base_type_val));

    // Verify it's a Type value
    if (base_type_val.kind != SDDL2_VALUE_TYPE) {
        return SDDL2_TYPE_MISMATCH;
    }

    // Cast to uint32_t (safe since we verified > 0)
    uint32_t array_count = (uint32_t)array_count_i64;

    // Check for multiplication overflow: width * array_count
    // For unsigned multiplication overflow check: if (a > 0 && b > UINT32_MAX /
    // a)
    uint32_t base_width = base_type_val.value.as_type.width;
    if (base_width > UINT32_MAX / array_count) {
        return SDDL2_STACK_OVERFLOW; // Width multiplication would overflow
    }

    // Create new type with multiplied width
    SDDL2_Type array_type = base_type_val.value.as_type;
    array_type.width      = base_width * array_count;

    // Push the array type back onto stack
    return SDDL2_Stack_push(stack, SDDL2_Value_type(array_type));
}

SDDL2_Error SDDL2_op_type_structure(
        SDDL2_Stack* stack,
        SDDL2_allocator_fn alloc_fn,
        void* alloc_ctx)
{
    // Pop member count (I64) from stack
    SDDL2_Value count_val;
    SDDL2_TRY(SDDL2_Stack_pop(stack, &count_val));

    // Verify it's an I64 value
    if (count_val.kind != SDDL2_VALUE_I64) {
        return SDDL2_TYPE_MISMATCH;
    }

    int64_t count_i64 = count_val.value.as_i64;

    // Validate count is positive
    if (count_i64 <= 0) {
        return SDDL2_TYPE_MISMATCH;
    }

    size_t member_count = (size_t)count_i64;

    // Calculate allocation size for structure data
    // size = sizeof(header) + member_count * sizeof(SDDL2_Type)
    size_t allocation_size = sizeof(SDDL2_Type_structure_data)
            + member_count * sizeof(SDDL2_Type);

    // Allocate structure data
    SDDL2_Type_structure_data* struct_data;
    if (alloc_fn != NULL) {
        struct_data = (SDDL2_Type_structure_data*)alloc_fn(
                alloc_ctx, allocation_size);
    } else {
        struct_data = (SDDL2_Type_structure_data*)sddl2_fallback_realloc(
                NULL, allocation_size);
    }

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

        SDDL2_Value type_val;
        SDDL2_TRY(SDDL2_Stack_pop(stack, &type_val));

        // Verify it's a Type value
        if (type_val.kind != SDDL2_VALUE_TYPE) {
            // Allocation leak here, but in arena mode it's OK
            // In test mode with malloc, this would leak
            sddl2_free(struct_data, alloc_fn);
            return SDDL2_TYPE_MISMATCH;
        }

        struct_data->members[index] = type_val.value.as_type;
    }

    // Compute total size by summing all member sizes
    for (size_t i = 0; i < member_count; i++) {
        size_t member_size = SDDL2_Type_size(struct_data->members[i]);

        // Check for size overflow
        if (struct_data->total_size_bytes > SIZE_MAX - member_size) {
            sddl2_free(struct_data, alloc_fn);
            return SDDL2_STACK_OVERFLOW; // Size overflow
        }

        struct_data->total_size_bytes += member_size;
    }

    // Create structure type
    SDDL2_Type struct_type = { .kind  = SDDL2_TYPE_STRUCTURE,
                               .width = 1, // Single instance (can be multiplied
                                           // later with type.fixed_array)
                               .complex_data = struct_data };

    // Push structure type onto stack
    return SDDL2_Stack_push(stack, SDDL2_Value_type(struct_type));
}

/* ============================================================================
 * Generic Stack Operation Helpers
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
 * Arithmetic Operations (Phase 2)
 * ========================================================================= */

/**
 * Helper: Check if addition would overflow.
 * Uses: (a > 0 && b > 0 && a > INT64_MAX - b) || (a < 0 && b < 0 && a <
 * INT64_MIN - b)
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

SDDL2_Error SDDL2_op_add(SDDL2_Stack* stack)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));

    if (add_would_overflow(a, b)) {
        return SDDL2_STACK_OVERFLOW;
    }

    return push_i64(stack, a + b);
}

SDDL2_Error SDDL2_op_sub(SDDL2_Stack* stack)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));

    if (sub_would_overflow(a, b)) {
        return SDDL2_STACK_OVERFLOW;
    }

    return push_i64(stack, a - b);
}

SDDL2_Error SDDL2_op_mul(SDDL2_Stack* stack)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));

    if (mul_would_overflow(a, b)) {
        return SDDL2_STACK_OVERFLOW;
    }

    return push_i64(stack, a * b);
}

SDDL2_Error SDDL2_op_div(SDDL2_Stack* stack)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));

    // Divide by zero check
    if (b == 0) {
        return SDDL2_DIV_ZERO;
    }

    // Overflow check: INT64_MIN / -1 = overflow
    if (a == INT64_MIN && b == -1) {
        return SDDL2_STACK_OVERFLOW;
    }

    return push_i64(stack, a / b);
}

SDDL2_Error SDDL2_op_mod(SDDL2_Stack* stack)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));

    // Divide by zero check
    if (b == 0) {
        return SDDL2_DIV_ZERO;
    }

    return push_i64(stack, a % b);
}

SDDL2_Error SDDL2_op_abs(SDDL2_Stack* stack)
{
    int64_t a;
    SDDL2_TRY(pop_i64(stack, &a));

    // Check for INT64_MIN (abs(INT64_MIN) overflows)
    if (a == INT64_MIN) {
        return SDDL2_STACK_OVERFLOW;
    }

    return push_i64(stack, (a < 0) ? -a : a);
}

SDDL2_Error SDDL2_op_neg(SDDL2_Stack* stack)
{
    int64_t a;
    SDDL2_TRY(pop_i64(stack, &a));

    // Check for INT64_MIN (negation overflows)
    if (a == INT64_MIN) {
        return SDDL2_STACK_OVERFLOW;
    }

    return push_i64(stack, -a);
}

/* ============================================================================
 * Comparison Operations (CMP Family)
 * ========================================================================= */

SDDL2_Error SDDL2_op_eq(SDDL2_Stack* stack)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));
    return push_i64(stack, (a == b) ? 1 : 0);
}

SDDL2_Error SDDL2_op_ne(SDDL2_Stack* stack)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));
    return push_i64(stack, (a != b) ? 1 : 0);
}

SDDL2_Error SDDL2_op_lt(SDDL2_Stack* stack)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));
    return push_i64(stack, (a < b) ? 1 : 0);
}

SDDL2_Error SDDL2_op_le(SDDL2_Stack* stack)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));
    return push_i64(stack, (a <= b) ? 1 : 0);
}

SDDL2_Error SDDL2_op_gt(SDDL2_Stack* stack)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));
    return push_i64(stack, (a > b) ? 1 : 0);
}

SDDL2_Error SDDL2_op_ge(SDDL2_Stack* stack)
{
    int64_t a, b;
    SDDL2_TRY(pop_binary_i64(stack, &a, &b));
    return push_i64(stack, (a >= b) ? 1 : 0);
}

/* ============================================================================
 * Stack Manipulation Operations (STACK Family)
 * ========================================================================= */

SDDL2_Error SDDL2_op_drop(SDDL2_Stack* stack)
{
    SDDL2_Value val;
    return SDDL2_Stack_pop(stack, &val);
}

SDDL2_Error SDDL2_op_dup(SDDL2_Stack* stack)
{
    SDDL2_Value val;
    SDDL2_TRY(SDDL2_Stack_peek(stack, &val));
    return SDDL2_Stack_push(stack, val);
}

SDDL2_Error SDDL2_op_swap(SDDL2_Stack* stack)
{
    SDDL2_Value a, b;
    SDDL2_TRY(SDDL2_Stack_pop(stack, &a));
    SDDL2_TRY(SDDL2_Stack_pop(stack, &b));
    SDDL2_TRY(SDDL2_Stack_push(stack, a));
    return SDDL2_Stack_push(stack, b);
}

/* ============================================================================
 * Input Buffer Operations (Phase 3)
 * ========================================================================= */

void SDDL2_Input_buffer_init(
        SDDL2_Input_buffer* buffer,
        const void* data,
        size_t size)
{
    buffer->data        = data;
    buffer->size        = size;
    buffer->current_pos = 0;
}

SDDL2_Error SDDL2_op_current_pos(
        SDDL2_Stack* stack,
        const SDDL2_Input_buffer* buffer)
{
    // Push current cursor position as I64
    return SDDL2_Stack_push(
            stack, SDDL2_Value_i64((int64_t)buffer->current_pos));
}

SDDL2_Error SDDL2_op_remaining(
        SDDL2_Stack* stack,
        const SDDL2_Input_buffer* buffer)
{
    // Push remaining bytes in input buffer as I64
    size_t remaining = buffer->size - buffer->current_pos;
    return SDDL2_Stack_push(stack, SDDL2_Value_i64((int64_t)remaining));
}

SDDL2_Error SDDL2_op_load_u8(
        SDDL2_Stack* stack,
        const SDDL2_Input_buffer* buffer)
{
    int64_t addr;
    SDDL2_TRY(pop_i64(stack, &addr));

    // Bounds check: 0 <= addr < size
    if (addr < 0 || (size_t)addr >= buffer->size) {
        return SDDL2_LOAD_BOUNDS;
    }

    // Load byte and push as I64 (zero-extended)
    const uint8_t* bytes = (const uint8_t*)buffer->data;
    return push_i64(stack, (int64_t)bytes[addr]);
}

SDDL2_Error SDDL2_op_load_i8(
        SDDL2_Stack* stack,
        const SDDL2_Input_buffer* buffer)
{
    int64_t addr;
    SDDL2_TRY(pop_i64(stack, &addr));

    // Bounds check: 0 <= addr < size
    if (addr < 0 || (size_t)addr >= buffer->size) {
        return SDDL2_LOAD_BOUNDS;
    }

    // Load byte and push as I64 (sign-extended)
    const int8_t* bytes = (const int8_t*)buffer->data;
    return push_i64(stack, (int64_t)bytes[addr]);
}

SDDL2_Error SDDL2_op_load_u16le(
        SDDL2_Stack* stack,
        const SDDL2_Input_buffer* buffer)
{
    int64_t addr;
    SDDL2_TRY(pop_i64(stack, &addr));

    // Bounds check: 0 <= addr <= size-2
    if (addr < 0 || (size_t)addr + 2 > buffer->size) {
        return SDDL2_LOAD_BOUNDS;
    }

    // Load 16-bit LE value and push as I64 (zero-extended)
    const uint8_t* bytes = (const uint8_t*)buffer->data;
    uint16_t value       = ZL_readLE16(&bytes[addr]);
    return push_i64(stack, (int64_t)value);
}

SDDL2_Error SDDL2_op_load_u16be(
        SDDL2_Stack* stack,
        const SDDL2_Input_buffer* buffer)
{
    int64_t addr;
    SDDL2_TRY(pop_i64(stack, &addr));

    // Bounds check: 0 <= addr <= size-2
    if (addr < 0 || (size_t)addr + 2 > buffer->size) {
        return SDDL2_LOAD_BOUNDS;
    }

    // Load 16-bit BE value and push as I64 (zero-extended)
    const uint8_t* bytes = (const uint8_t*)buffer->data;
    uint16_t value       = ZL_readBE16(&bytes[addr]);
    return push_i64(stack, (int64_t)value);
}

SDDL2_Error SDDL2_op_load_i16le(
        SDDL2_Stack* stack,
        const SDDL2_Input_buffer* buffer)
{
    int64_t addr;
    SDDL2_TRY(pop_i64(stack, &addr));

    // Bounds check: 0 <= addr <= size-2
    if (addr < 0 || (size_t)addr + 2 > buffer->size) {
        return SDDL2_LOAD_BOUNDS;
    }

    // Load 16-bit LE value and push as I64 (sign-extended)
    const uint8_t* bytes = (const uint8_t*)buffer->data;
    int16_t value        = (int16_t)ZL_readLE16(&bytes[addr]);
    return push_i64(stack, (int64_t)value);
}

SDDL2_Error SDDL2_op_load_i16be(
        SDDL2_Stack* stack,
        const SDDL2_Input_buffer* buffer)
{
    int64_t addr;
    SDDL2_TRY(pop_i64(stack, &addr));

    // Bounds check: 0 <= addr <= size-2
    if (addr < 0 || (size_t)addr + 2 > buffer->size) {
        return SDDL2_LOAD_BOUNDS;
    }

    // Load 16-bit BE value and push as I64 (sign-extended)
    const uint8_t* bytes = (const uint8_t*)buffer->data;
    int16_t value        = (int16_t)ZL_readBE16(&bytes[addr]);
    return push_i64(stack, (int64_t)value);
}

SDDL2_Error SDDL2_op_load_u32le(
        SDDL2_Stack* stack,
        const SDDL2_Input_buffer* buffer)
{
    int64_t addr;
    SDDL2_TRY(pop_i64(stack, &addr));

    // Bounds check: 0 <= addr <= size-4
    if (addr < 0 || (size_t)addr + 4 > buffer->size) {
        return SDDL2_LOAD_BOUNDS;
    }

    // Load 32-bit LE value and push as I64 (zero-extended)
    const uint8_t* bytes = (const uint8_t*)buffer->data;
    uint32_t value       = ZL_readLE32(&bytes[addr]);
    return push_i64(stack, (int64_t)value);
}

SDDL2_Error SDDL2_op_load_u32be(
        SDDL2_Stack* stack,
        const SDDL2_Input_buffer* buffer)
{
    int64_t addr;
    SDDL2_TRY(pop_i64(stack, &addr));

    // Bounds check: 0 <= addr <= size-4
    if (addr < 0 || (size_t)addr + 4 > buffer->size) {
        return SDDL2_LOAD_BOUNDS;
    }

    // Load 32-bit BE value and push as I64 (zero-extended)
    const uint8_t* bytes = (const uint8_t*)buffer->data;
    uint32_t value       = ZL_readBE32(&bytes[addr]);
    return push_i64(stack, (int64_t)value);
}

SDDL2_Error SDDL2_op_load_i32le(
        SDDL2_Stack* stack,
        const SDDL2_Input_buffer* buffer)
{
    int64_t addr;
    SDDL2_TRY(pop_i64(stack, &addr));

    // Bounds check: 0 <= addr <= size-4
    if (addr < 0 || (size_t)addr + 4 > buffer->size) {
        return SDDL2_LOAD_BOUNDS;
    }

    // Load 32-bit LE value and push as I64 (sign-extended)
    const uint8_t* bytes = (const uint8_t*)buffer->data;
    int32_t value        = (int32_t)ZL_readLE32(&bytes[addr]);
    return push_i64(stack, (int64_t)value);
}

SDDL2_Error SDDL2_op_load_i32be(
        SDDL2_Stack* stack,
        const SDDL2_Input_buffer* buffer)
{
    int64_t addr;
    SDDL2_TRY(pop_i64(stack, &addr));

    // Bounds check: 0 <= addr <= size-4
    if (addr < 0 || (size_t)addr + 4 > buffer->size) {
        return SDDL2_LOAD_BOUNDS;
    }

    // Load 32-bit BE value and push as I64 (sign-extended)
    const uint8_t* bytes = (const uint8_t*)buffer->data;
    int32_t value        = (int32_t)ZL_readBE32(&bytes[addr]);
    return push_i64(stack, (int64_t)value);
}

SDDL2_Error SDDL2_op_load_i64le(
        SDDL2_Stack* stack,
        const SDDL2_Input_buffer* buffer)
{
    int64_t addr;
    SDDL2_TRY(pop_i64(stack, &addr));

    // Bounds check: 0 <= addr <= size-8
    if (addr < 0 || (size_t)addr + 8 > buffer->size) {
        return SDDL2_LOAD_BOUNDS;
    }

    // Load 64-bit LE value and push as I64
    const uint8_t* bytes = (const uint8_t*)buffer->data;
    int64_t value        = (int64_t)ZL_readLE64(&bytes[addr]);
    return push_i64(stack, value);
}

SDDL2_Error SDDL2_op_load_i64be(
        SDDL2_Stack* stack,
        const SDDL2_Input_buffer* buffer)
{
    int64_t addr;
    SDDL2_TRY(pop_i64(stack, &addr));

    // Bounds check: 0 <= addr <= size-8
    if (addr < 0 || (size_t)addr + 8 > buffer->size) {
        return SDDL2_LOAD_BOUNDS;
    }

    // Load 64-bit BE value and push as I64
    const uint8_t* bytes = (const uint8_t*)buffer->data;
    int64_t value        = (int64_t)ZL_readBE64(&bytes[addr]);
    return push_i64(stack, value);
}

/* ============================================================================
 * Segment Operations (Phase 4-5)
 * ========================================================================= */

/* ============================================================================
 * Memory Management Abstraction Layer
 * ========================================================================= */

// Forward declarations
static int tag_registry_register(SDDL2_Tag_registry* registry, uint32_t tag);

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

    // Pre-allocate capacity when using arena allocator
    // This reduces reallocation overhead since arena allocation
    // requires allocate+copy (unlike realloc which may grow in place)
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
        SDDL2_Input_buffer* buffer,
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
    if (element_count > SIZE_MAX / total_type_size) {
        return SDDL2_STACK_OVERFLOW; // Size overflow
    }

    size_t size_bytes = element_count * total_type_size;

    // Bounds check: segment must fit in remaining buffer
    if (buffer->current_pos + size_bytes > buffer->size) {
        return SDDL2_SEGMENT_BOUNDS;
    }

    // Register tag if non-zero (tagged segments only)
    if (tag != 0) {
        if (!tag_registry_register(registry, tag)) {
            return SDDL2_LIMIT_EXCEEDED; // Capacity limit exceeded or
                                         // allocation allocation failed
        }
    }

    // Check if we can merge with the last segment
    // Merge conditions: same tag AND same type AND consecutive positions
    // This applies to ALL segments, including unspecified ones (tag=0)
    // Unspecified segments merge to reduce overhead for "leftover" data
    if (segments->count > 0) {
        SDDL2_Segment* last = &segments->items[segments->count - 1];
        size_t expected_pos = last->start_pos + last->size_bytes;

        // Check if types match (both kind and width)
        bool types_match =
                (last->type.kind == type.kind
                 && last->type.width == type.width);

        if (last->tag == tag && types_match
            && expected_pos == buffer->current_pos) {
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
        SDDL2_Input_buffer* buffer,
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
 * Tag Registry Operations (Phase 5)
 * ========================================================================= */

void SDDL2_Tag_registry_init(
        SDDL2_Tag_registry* registry,
        SDDL2_allocator_fn alloc_fn,
        void* alloc_ctx)
{
    registry->tags      = NULL;
    registry->count     = 0;
    registry->capacity  = 0;
    registry->alloc_fn  = alloc_fn;
    registry->alloc_ctx = alloc_ctx;

    // Pre-allocate capacity when using arena allocator
    // This reduces reallocation overhead since arena allocation
    // requires allocate+copy (unlike realloc which may grow in place)
    if (alloc_fn != NULL) {
        size_t initial_size = SDDL2_TAG_INITIAL_CAPACITY * sizeof(uint32_t);
        registry->tags      = (uint32_t*)alloc_fn(alloc_ctx, initial_size);
        if (registry->tags != NULL) {
            registry->capacity = SDDL2_TAG_INITIAL_CAPACITY;
        }
        // If allocation fails, capacity remains 0 and will be handled
        // by tag_registry_register() when first tag is registered
    }
}

void SDDL2_Tag_registry_destroy(SDDL2_Tag_registry* registry)
{
    sddl2_free(registry->tags, registry->alloc_fn);
    registry->tags     = NULL;
    registry->count    = 0;
    registry->capacity = 0;
}

/**
 * Helper: Register a tag if not already registered.
 * Returns 1 on success, 0 on allocation failure or capacity limit exceeded.
 *
 * Uses the unified sddl2_realloc() abstraction which handles both
 * arena allocation and heap allocation transparently.
 */
static int tag_registry_register(SDDL2_Tag_registry* registry, uint32_t tag)
{
    // Check if tag is already registered
    for (size_t i = 0; i < registry->count; i++) {
        if (registry->tags[i] == tag) {
            return 1; // Already registered
        }
    }

    // Ensure capacity for new tag
    if (!ensure_capacity(
                (void*)&registry->tags,
                registry->count,
                &registry->capacity,
                sizeof(uint32_t),
                SDDL2_TAG_MAX_CAPACITY,
                registry->alloc_fn,
                registry->alloc_ctx)) {
        return 0; // Allocation failed or capacity limit reached
    }

    // Register tag
    registry->tags[registry->count++] = tag;
    return 1;
}

SDDL2_Error SDDL2_op_segment_create_tagged(
        SDDL2_Stack* stack,
        SDDL2_Input_buffer* buffer,
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
