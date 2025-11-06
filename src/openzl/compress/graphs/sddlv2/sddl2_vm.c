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

/* ============================================================================
 * Stack Operations - Non-Critical Path
 * ========================================================================= */

void SDDL2_stack_init(SDDL2_stack* stack)
{
    stack->top = 0;
}

SDDL2_error SDDL2_stack_peek(const SDDL2_stack* stack, SDDL2_value* out)
{
    if (stack->top == 0) {
        return SDDL2_STACK_UNDERFLOW;
    }
    *out = stack->items[stack->top - 1];
    return SDDL2_OK;
}

size_t SDDL2_stack_depth(const SDDL2_stack* stack)
{
    return stack->top;
}

int SDDL2_stack_is_empty(const SDDL2_stack* stack)
{
    return stack->top == 0;
}

/* ============================================================================
 * Type Utilities
 * ========================================================================= */

size_t SDDL2_type_size(SDDL2_type_kind kind)
{
    switch (kind) {
        case SDDL2_TYPE_U8:
        case SDDL2_TYPE_I8:
            return 1;
        case SDDL2_TYPE_U16LE:
        case SDDL2_TYPE_U16BE:
        case SDDL2_TYPE_I16LE:
        case SDDL2_TYPE_I16BE:
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
        default:
            return 0; // Unknown type
    }
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

SDDL2_error SDDL2_op_add(SDDL2_stack* stack)
{
    // Pop two operands
    SDDL2_value b, a;
    SDDL2_error err;

    if ((err = SDDL2_stack_pop(stack, &b)) != SDDL2_OK)
        return err;
    if ((err = SDDL2_stack_pop(stack, &a)) != SDDL2_OK)
        return err;

    // Type check
    if (a.kind != SDDL2_VALUE_I64 || b.kind != SDDL2_VALUE_I64) {
        return SDDL2_TYPE_MISMATCH;
    }

    // Overflow check
    if (add_would_overflow(a.value.as_i64, b.value.as_i64)) {
        return SDDL2_STACK_OVERFLOW;
    }

    // Perform addition and push result
    int64_t result = a.value.as_i64 + b.value.as_i64;
    return SDDL2_stack_push(stack, SDDL2_value_i64(result));
}

SDDL2_error SDDL2_op_sub(SDDL2_stack* stack)
{
    // Pop two operands
    SDDL2_value b, a;
    SDDL2_error err;

    if ((err = SDDL2_stack_pop(stack, &b)) != SDDL2_OK)
        return err;
    if ((err = SDDL2_stack_pop(stack, &a)) != SDDL2_OK)
        return err;

    // Type check
    if (a.kind != SDDL2_VALUE_I64 || b.kind != SDDL2_VALUE_I64) {
        return SDDL2_TYPE_MISMATCH;
    }

    // Overflow check
    if (sub_would_overflow(a.value.as_i64, b.value.as_i64)) {
        return SDDL2_STACK_OVERFLOW;
    }

    // Perform subtraction and push result
    int64_t result = a.value.as_i64 - b.value.as_i64;
    return SDDL2_stack_push(stack, SDDL2_value_i64(result));
}

SDDL2_error SDDL2_op_mul(SDDL2_stack* stack)
{
    // Pop two operands
    SDDL2_value b, a;
    SDDL2_error err;

    if ((err = SDDL2_stack_pop(stack, &b)) != SDDL2_OK)
        return err;
    if ((err = SDDL2_stack_pop(stack, &a)) != SDDL2_OK)
        return err;

    // Type check
    if (a.kind != SDDL2_VALUE_I64 || b.kind != SDDL2_VALUE_I64) {
        return SDDL2_TYPE_MISMATCH;
    }

    // Overflow check
    if (mul_would_overflow(a.value.as_i64, b.value.as_i64)) {
        return SDDL2_STACK_OVERFLOW;
    }

    // Perform multiplication and push result
    int64_t result = a.value.as_i64 * b.value.as_i64;
    return SDDL2_stack_push(stack, SDDL2_value_i64(result));
}

SDDL2_error SDDL2_op_div(SDDL2_stack* stack)
{
    // Pop two operands
    SDDL2_value b, a;
    SDDL2_error err;

    if ((err = SDDL2_stack_pop(stack, &b)) != SDDL2_OK)
        return err;
    if ((err = SDDL2_stack_pop(stack, &a)) != SDDL2_OK)
        return err;

    // Type check
    if (a.kind != SDDL2_VALUE_I64 || b.kind != SDDL2_VALUE_I64) {
        return SDDL2_TYPE_MISMATCH;
    }

    // Divide by zero check
    if (b.value.as_i64 == 0) {
        return SDDL2_STACK_OVERFLOW; // Using OVERFLOW for div-by-zero (TODO:
                                     // add DIVZERO error)
    }

    // Overflow check: INT64_MIN / -1 = overflow
    if (a.value.as_i64 == INT64_MIN && b.value.as_i64 == -1) {
        return SDDL2_STACK_OVERFLOW;
    }

    // Perform division and push result
    int64_t result = a.value.as_i64 / b.value.as_i64;
    return SDDL2_stack_push(stack, SDDL2_value_i64(result));
}

SDDL2_error SDDL2_op_mod(SDDL2_stack* stack)
{
    // Pop two operands
    SDDL2_value b, a;
    SDDL2_error err;

    if ((err = SDDL2_stack_pop(stack, &b)) != SDDL2_OK)
        return err;
    if ((err = SDDL2_stack_pop(stack, &a)) != SDDL2_OK)
        return err;

    // Type check
    if (a.kind != SDDL2_VALUE_I64 || b.kind != SDDL2_VALUE_I64) {
        return SDDL2_TYPE_MISMATCH;
    }

    // Divide by zero check
    if (b.value.as_i64 == 0) {
        return SDDL2_STACK_OVERFLOW; // Using OVERFLOW for div-by-zero (TODO:
                                     // add DIVZERO error)
    }

    // Perform modulo and push result
    int64_t result = a.value.as_i64 % b.value.as_i64;
    return SDDL2_stack_push(stack, SDDL2_value_i64(result));
}

SDDL2_error SDDL2_op_abs(SDDL2_stack* stack)
{
    // Pop operand
    SDDL2_value a;
    SDDL2_error err;

    if ((err = SDDL2_stack_pop(stack, &a)) != SDDL2_OK)
        return err;

    // Type check
    if (a.kind != SDDL2_VALUE_I64) {
        return SDDL2_TYPE_MISMATCH;
    }

    // Check for INT64_MIN (abs(INT64_MIN) overflows)
    if (a.value.as_i64 == INT64_MIN) {
        return SDDL2_STACK_OVERFLOW;
    }

    // Perform absolute value and push result
    int64_t result = (a.value.as_i64 < 0) ? -a.value.as_i64 : a.value.as_i64;
    return SDDL2_stack_push(stack, SDDL2_value_i64(result));
}

SDDL2_error SDDL2_op_neg(SDDL2_stack* stack)
{
    // Pop operand
    SDDL2_value a;
    SDDL2_error err;

    if ((err = SDDL2_stack_pop(stack, &a)) != SDDL2_OK)
        return err;

    // Type check
    if (a.kind != SDDL2_VALUE_I64) {
        return SDDL2_TYPE_MISMATCH;
    }

    // Check for INT64_MIN (negation overflows)
    if (a.value.as_i64 == INT64_MIN) {
        return SDDL2_STACK_OVERFLOW;
    }

    // Perform negation and push result
    int64_t result = -a.value.as_i64;
    return SDDL2_stack_push(stack, SDDL2_value_i64(result));
}

/* ============================================================================
 * Input Buffer Operations (Phase 3)
 * ========================================================================= */

void SDDL2_input_buffer_init(
        SDDL2_input_buffer* buffer,
        const void* data,
        size_t size)
{
    buffer->data        = data;
    buffer->size        = size;
    buffer->current_pos = 0;
}

SDDL2_error SDDL2_op_current_pos(
        SDDL2_stack* stack,
        const SDDL2_input_buffer* buffer)
{
    // Push current cursor position as I64
    return SDDL2_stack_push(
            stack, SDDL2_value_i64((int64_t)buffer->current_pos));
}

SDDL2_error SDDL2_op_load_u8(
        SDDL2_stack* stack,
        const SDDL2_input_buffer* buffer)
{
    // Pop address from stack
    SDDL2_value addr_val;
    SDDL2_error err;

    if ((err = SDDL2_stack_pop(stack, &addr_val)) != SDDL2_OK)
        return err;

    // Type check
    if (addr_val.kind != SDDL2_VALUE_I64) {
        return SDDL2_TYPE_MISMATCH;
    }

    int64_t addr = addr_val.value.as_i64;

    // Bounds check: 0 <= addr < size
    if (addr < 0 || (size_t)addr >= buffer->size) {
        return SDDL2_LOAD_BOUNDS;
    }

    // Load byte and push as I64 (zero-extended)
    const uint8_t* bytes = (const uint8_t*)buffer->data;
    uint8_t byte_val     = bytes[addr];
    return SDDL2_stack_push(stack, SDDL2_value_i64((int64_t)byte_val));
}
