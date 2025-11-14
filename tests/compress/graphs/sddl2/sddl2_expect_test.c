// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Comprehensive tests for SDDL2 EXPECT operations
 * Tests validation operations (expect_true)
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>  // printf
#include <stdlib.h> // malloc, free
#include "openzl/compress/graphs/sddl2/sddl2_vm.h"
#include "sddl2_test_framework.h"

/* ============================================================================
 * Test Setup Helpers
 * ========================================================================= */

static SDDL2_Stack* create_test_stack(size_t capacity)
{
    SDDL2_Stack* stack = malloc(sizeof(SDDL2_Stack));
    assert(stack != NULL);

    stack->items = malloc(sizeof(SDDL2_Value) * capacity);
    assert(stack->items != NULL);

    stack->capacity = capacity;
    SDDL2_Stack_init(stack);

    return stack;
}

static void destroy_test_stack(SDDL2_Stack* stack)
{
    free(stack->items);
    free(stack);
}

/* ============================================================================
 * expect_true Success Tests
 * ========================================================================= */

TEST(test_expect_true_positive_value)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: expect_true with positive non-zero value should succeed
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_OK);
    assert(SDDL2_Stack_depth(stack) == 0); // Value consumed

    destroy_test_stack(stack);
}

TEST(test_expect_true_large_positive)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: expect_true with large positive value should succeed
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(123456789)) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_OK);
    assert(SDDL2_Stack_depth(stack) == 0);

    destroy_test_stack(stack);
}

TEST(test_expect_true_negative_value)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: expect_true with negative value (non-zero) should succeed
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_OK);
    assert(SDDL2_Stack_depth(stack) == 0);

    destroy_test_stack(stack);
}

TEST(test_expect_true_large_negative)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: expect_true with large negative value should succeed
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-999999)) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_OK);
    assert(SDDL2_Stack_depth(stack) == 0);

    destroy_test_stack(stack);
}

TEST(test_expect_true_int64_max)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: expect_true with INT64_MAX should succeed
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(INT64_MAX)) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_OK);
    assert(SDDL2_Stack_depth(stack) == 0);

    destroy_test_stack(stack);
}

TEST(test_expect_true_int64_min)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: expect_true with INT64_MIN (most negative) should succeed
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(INT64_MIN)) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_OK);
    assert(SDDL2_Stack_depth(stack) == 0);

    destroy_test_stack(stack);
}

/* ============================================================================
 * expect_true Failure Tests
 * ========================================================================= */

TEST(test_expect_true_zero_fails)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: expect_true with zero should fail with VALIDATION_FAILED
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_VALIDATION_FAILED);
    // Stack state after error is implementation-defined, but value should be consumed
    assert(SDDL2_Stack_depth(stack) == 0);

    destroy_test_stack(stack);
}

/* ============================================================================
 * expect_true Error Condition Tests
 * ========================================================================= */

TEST(test_expect_true_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: expect_true on empty stack should fail with STACK_UNDERFLOW
    assert(SDDL2_op_expect_true(stack) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_expect_true_type_mismatch_tag)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: expect_true with Tag instead of I64 should fail with TYPE_MISMATCH
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(100)) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_expect_true_type_mismatch_type)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: expect_true with Type instead of I64 should fail with TYPE_MISMATCH
    SDDL2_Type type = { .kind = SDDL2_TYPE_U8, .width = 1 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

/* ============================================================================
 * expect_true Combined with Comparison Operations
 * ========================================================================= */

TEST(test_expect_true_with_cmp_eq_success)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: cmp.eq followed by expect_true (equal values)
    // 42 == 42 -> 1 -> expect_true succeeds
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_eq(stack) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_OK);
    assert(SDDL2_Stack_depth(stack) == 0);

    destroy_test_stack(stack);
}

TEST(test_expect_true_with_cmp_eq_failure)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: cmp.eq followed by expect_true (different values)
    // 42 == 99 -> 0 -> expect_true fails
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(99)) == SDDL2_OK);
    assert(SDDL2_op_eq(stack) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_VALIDATION_FAILED);

    destroy_test_stack(stack);
}

TEST(test_expect_true_with_cmp_ne_success)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: cmp.ne followed by expect_true (different values)
    // 42 != 99 -> 1 -> expect_true succeeds
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(99)) == SDDL2_OK);
    assert(SDDL2_op_ne(stack) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_OK);
    assert(SDDL2_Stack_depth(stack) == 0);

    destroy_test_stack(stack);
}

TEST(test_expect_true_with_cmp_lt_success)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: cmp.lt followed by expect_true
    // 10 < 20 -> 1 -> expect_true succeeds
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(10)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(20)) == SDDL2_OK);
    assert(SDDL2_op_lt(stack) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_OK);

    destroy_test_stack(stack);
}

TEST(test_expect_true_with_cmp_gt_failure)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: cmp.gt followed by expect_true (false condition)
    // 10 > 20 -> 0 -> expect_true fails
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(10)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(20)) == SDDL2_OK);
    assert(SDDL2_op_gt(stack) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_VALIDATION_FAILED);

    destroy_test_stack(stack);
}

/* ============================================================================
 * expect_true Combined with Logic Operations
 * ========================================================================= */

TEST(test_expect_true_with_logic_not)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: logic.not followed by expect_true (expect_false pattern)
    // push 0 -> not -> -1 -> expect_true succeeds
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_not(stack) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_OK);

    destroy_test_stack(stack);
}

TEST(test_expect_true_with_logic_and)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: logic.and followed by expect_true
    // 0xFF & 0x0F = 0x0F (non-zero) -> expect_true succeeds
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0xFF)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x0F)) == SDDL2_OK);
    assert(SDDL2_op_and(stack) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_OK);

    destroy_test_stack(stack);
}

TEST(test_expect_true_with_logic_and_zero_result)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: logic.and followed by expect_true (zero result)
    // 0xFF & 0x00 = 0 -> expect_true fails
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0xFF)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x00)) == SDDL2_OK);
    assert(SDDL2_op_and(stack) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_VALIDATION_FAILED);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Multiple expect_true Tests
 * ========================================================================= */

TEST(test_multiple_expect_true_all_pass)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: Multiple expect_true operations, all should succeed
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_OK);
    
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_OK);
    
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_OK);
    
    assert(SDDL2_Stack_depth(stack) == 0);

    destroy_test_stack(stack);
}

TEST(test_multiple_expect_true_first_fails)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: First expect_true fails, subsequent ops not reached
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack) == SDDL2_VALIDATION_FAILED);

    destroy_test_stack(stack);
}

int main(void)
{
    return sddl2_run_all_tests();
}
