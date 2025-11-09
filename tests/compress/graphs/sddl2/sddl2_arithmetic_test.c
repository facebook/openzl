// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Unit tests for OpenZL VM arithmetic operations.
 * Tests Phase 2: Basic Arithmetic
 */

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "openzl/compress/graphs/sddl2/sddl2_vm.h"
#include "sddl2_test_framework.h"

/* Test helper: Create a stack for testing */
static SDDL2_stack* create_test_stack(size_t capacity)
{
    SDDL2_stack* stack = malloc(sizeof(SDDL2_stack));
    assert(stack != NULL);

    stack->items = malloc(sizeof(SDDL2_value) * capacity);
    assert(stack->items != NULL);

    stack->capacity = capacity;
    SDDL2_stack_init(stack);

    return stack;
}

static void destroy_test_stack(SDDL2_stack* stack)
{
    free(stack->items);
    free(stack);
}

/* ============================================================================
 * Basic Operation Tests
 * ========================================================================= */

TEST(test_add_basic)
{
    SDDL2_stack* stack = create_test_stack(100);

    // 5 + 3 = 8
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(5)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(3)) == SDDL2_OK);
    assert(SDDL2_op_add(stack) == SDDL2_OK);

    POP_AND_VERIFY_I64(stack, 8);

    destroy_test_stack(stack);
}

TEST(test_sub_basic)
{
    SDDL2_stack* stack = create_test_stack(100);

    // 10 - 4 = 6
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(10)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(4)) == SDDL2_OK);
    assert(SDDL2_op_sub(stack) == SDDL2_OK);

    POP_AND_VERIFY_I64(stack, 6);

    destroy_test_stack(stack);
}

TEST(test_mul_basic)
{
    SDDL2_stack* stack = create_test_stack(100);

    // 7 * 6 = 42
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(7)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(6)) == SDDL2_OK);
    assert(SDDL2_op_mul(stack) == SDDL2_OK);

    POP_AND_VERIFY_I64(stack, 42);

    destroy_test_stack(stack);
}

TEST(test_div_basic)
{
    SDDL2_stack* stack = create_test_stack(100);

    // 20 / 4 = 5
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(20)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(4)) == SDDL2_OK);
    assert(SDDL2_op_div(stack) == SDDL2_OK);

    POP_AND_VERIFY_I64(stack, 5);

    destroy_test_stack(stack);
}

TEST(test_mod_basic)
{
    SDDL2_stack* stack = create_test_stack(100);

    // 17 % 5 = 2
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(17)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(5)) == SDDL2_OK);
    assert(SDDL2_op_mod(stack) == SDDL2_OK);

    POP_AND_VERIFY_I64(stack, 2);

    destroy_test_stack(stack);
}

TEST(test_abs_basic)
{
    SDDL2_stack* stack = create_test_stack(100);

    // abs(-42) = 42
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-42)) == SDDL2_OK);
    assert(SDDL2_op_abs(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 42);

    // abs(42) = 42
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_abs(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 42);

    destroy_test_stack(stack);
}

TEST(test_neg_basic)
{
    SDDL2_stack* stack = create_test_stack(100);

    // -(-42) = 42
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-42)) == SDDL2_OK);
    assert(SDDL2_op_neg(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 42);

    // -(42) = -42
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_neg(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, -42);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Overflow Detection Tests
 * ========================================================================= */

TEST(test_add_overflow)
{
    SDDL2_stack* stack = create_test_stack(100);

    // INT64_MAX + 1 = overflow
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MAX)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_add(stack) == SDDL2_STACK_OVERFLOW);

    // INT64_MIN + (-1) = overflow
    SDDL2_stack_init(stack);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MIN)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_op_add(stack) == SDDL2_STACK_OVERFLOW);

    destroy_test_stack(stack);
}

TEST(test_sub_overflow)
{
    SDDL2_stack* stack = create_test_stack(100);

    // INT64_MIN - 1 = overflow
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MIN)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_sub(stack) == SDDL2_STACK_OVERFLOW);

    // INT64_MAX - (-1) = overflow
    SDDL2_stack_init(stack);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MAX)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_op_sub(stack) == SDDL2_STACK_OVERFLOW);

    destroy_test_stack(stack);
}

TEST(test_mul_overflow)
{
    SDDL2_stack* stack = create_test_stack(100);

    // INT64_MAX * 2 = overflow
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MAX)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(2)) == SDDL2_OK);
    assert(SDDL2_op_mul(stack) == SDDL2_STACK_OVERFLOW);

    // INT64_MIN * (-1) = overflow
    SDDL2_stack_init(stack);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MIN)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_op_mul(stack) == SDDL2_STACK_OVERFLOW);

    destroy_test_stack(stack);
}

TEST(test_abs_overflow)
{
    SDDL2_stack* stack = create_test_stack(100);

    // abs(INT64_MIN) = overflow
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MIN)) == SDDL2_OK);
    assert(SDDL2_op_abs(stack) == SDDL2_STACK_OVERFLOW);

    destroy_test_stack(stack);
}

TEST(test_neg_overflow)
{
    SDDL2_stack* stack = create_test_stack(100);

    // -(INT64_MIN) = overflow
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MIN)) == SDDL2_OK);
    assert(SDDL2_op_neg(stack) == SDDL2_STACK_OVERFLOW);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Divide-by-Zero Tests
 * ========================================================================= */

TEST(test_div_by_zero)
{
    SDDL2_stack* stack = create_test_stack(100);

    // 10 / 0 = error
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(10)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_div(stack) == SDDL2_DIV_ZERO);

    destroy_test_stack(stack);
}

TEST(test_mod_by_zero)
{
    SDDL2_stack* stack = create_test_stack(100);

    // 10 % 0 = error
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(10)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_mod(stack) == SDDL2_DIV_ZERO);

    destroy_test_stack(stack);
}

TEST(test_div_overflow_special)
{
    SDDL2_stack* stack = create_test_stack(100);

    // INT64_MIN / -1 = overflow
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MIN)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_op_div(stack) == SDDL2_STACK_OVERFLOW);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Type Mismatch Tests
 * ========================================================================= */

TEST(test_add_type_mismatch)
{
    SDDL2_stack* stack = create_test_stack(100);

    // I64 + Tag = type mismatch
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(5)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_tag(100)) == SDDL2_OK);
    assert(SDDL2_op_add(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_mul_type_mismatch)
{
    SDDL2_stack* stack = create_test_stack(100);

    // Tag * I64 = type mismatch
    SDDL2_type t = { .kind = SDDL2_TYPE_I32LE, .width = 1 };
    assert(SDDL2_stack_push(stack, SDDL2_value_type(t)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(5)) == SDDL2_OK);
    assert(SDDL2_op_mul(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_abs_type_mismatch)
{
    SDDL2_stack* stack = create_test_stack(100);

    // abs(Tag) = type mismatch
    assert(SDDL2_stack_push(stack, SDDL2_value_tag(42)) == SDDL2_OK);
    assert(SDDL2_op_abs(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Stack Underflow Tests
 * ========================================================================= */

TEST(test_add_underflow)
{
    SDDL2_stack* stack = create_test_stack(100);

    // Empty stack - should underflow
    assert(SDDL2_op_add(stack) == SDDL2_STACK_UNDERFLOW);

    // Only one operand - should underflow
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(5)) == SDDL2_OK);
    assert(SDDL2_op_add(stack) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_neg_underflow)
{
    SDDL2_stack* stack = create_test_stack(100);

    // Empty stack - should underflow
    assert(SDDL2_op_neg(stack) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Edge Case Tests
 * ========================================================================= */

TEST(test_zero_operations)
{
    SDDL2_stack* stack = create_test_stack(100);

    // 0 + 0 = 0
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(0)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_add(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    // 0 * 1000 = 0
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(0)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(1000)) == SDDL2_OK);
    assert(SDDL2_op_mul(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    // abs(0) = 0
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_abs(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

TEST(test_negative_operations)
{
    SDDL2_stack* stack = create_test_stack(100);

    // -5 + 3 = -2
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-5)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(3)) == SDDL2_OK);
    assert(SDDL2_op_add(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, -2);

    // -10 / -2 = 5
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-10)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-2)) == SDDL2_OK);
    assert(SDDL2_op_div(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 5);

    // -17 % 5 = -2 (C89/C99 behavior)
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-17)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(5)) == SDDL2_OK);
    assert(SDDL2_op_mod(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, -2);

    destroy_test_stack(stack);
}

int main(void)
{
    return sddl2_run_all_tests();
}
