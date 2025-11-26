// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Unit tests for stack.drop_if operation.
 *
 * Tests the conditional stack drop operation which:
 * - Pops a condition (I64)
 * - If condition is non-zero, pops and discards the top value
 * - If condition is zero, leaves the top value on the stack
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
 * Basic Functionality Tests
 * ========================================================================= */

TEST(test_drop_if_true_basic)
{
    SDDL2_Stack* stack = create_test_stack(10);

    // Push value then condition=1 (true)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);

    // Execute drop_if - should drop the 42
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_OK);

    // Stack should be empty
    assert(stack->top == 0);

    destroy_test_stack(stack);
}

TEST(test_drop_if_false_basic)
{
    SDDL2_Stack* stack = create_test_stack(10);

    // Push value then condition=0 (false)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);

    // Execute drop_if - should NOT drop the 42
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_OK);

    // Stack should contain the 42
    assert(stack->top == 1);

    SDDL2_Value result;
    assert(SDDL2_Stack_pop(stack, &result) == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_I64);
    assert(result.value.as_i64 == 42);

    destroy_test_stack(stack);
}

TEST(test_drop_if_nonzero_is_true)
{
    SDDL2_Stack* stack = create_test_stack(10);

    // Push value then condition=100 (non-zero = true)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(99)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(100)) == SDDL2_OK);

    // Execute drop_if - should drop the 99
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_OK);

    // Stack should be empty
    assert(stack->top == 0);

    destroy_test_stack(stack);
}

TEST(test_drop_if_negative_is_true)
{
    SDDL2_Stack* stack = create_test_stack(10);

    // Push value then condition=-1 (negative = true)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(50)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-1)) == SDDL2_OK);

    // Execute drop_if - should drop the 50
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_OK);

    // Stack should be empty
    assert(stack->top == 0);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Type Interactions
 * ========================================================================= */

TEST(test_drop_if_with_different_value_types)
{
    SDDL2_Stack* stack = create_test_stack(10);

    // Test with Tag value (should work - any value type can be dropped)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(123)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_OK);
    assert(stack->top == 0);

    // Test with Type value
    SDDL2_Type type = { .kind = SDDL2_TYPE_U8, .width = 1 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_OK);
    assert(stack->top == 0);

    destroy_test_stack(stack);
}

TEST(test_drop_if_preserves_value_type)
{
    SDDL2_Stack* stack = create_test_stack(10);

    // Push Tag value and condition=0 (false)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(999)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);

    // Execute drop_if - should NOT drop
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_OK);

    // Verify Tag value is preserved
    SDDL2_Value result;
    assert(SDDL2_Stack_pop(stack, &result) == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_TAG);
    assert(result.value.as_tag == 999);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Error Cases
 * ========================================================================= */

TEST(test_drop_if_underflow_empty_stack)
{
    SDDL2_Stack* stack = create_test_stack(10);

    // Empty stack - should fail
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_drop_if_underflow_only_condition)
{
    SDDL2_Stack* stack = create_test_stack(10);

    // Only condition, no value to drop
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);

    // Should fail when trying to drop non-existent value
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_drop_if_type_mismatch_condition)
{
    SDDL2_Stack* stack = create_test_stack(10);

    // Push value then Tag as condition (not I64)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(100)) == SDDL2_OK);

    // Should fail - condition must be I64
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Stack State Tests
 * ========================================================================= */

TEST(test_drop_if_multiple_values_on_stack)
{
    SDDL2_Stack* stack = create_test_stack(10);

    // Push several values, then drop_if on top one
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(10)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(20)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(30))
           == SDDL2_OK); // value to conditionally drop
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1))
           == SDDL2_OK); // condition=true

    // Drop the 30
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_OK);

    // Stack should have 10, 20 remaining
    assert(stack->top == 2);

    SDDL2_Value v1, v2;
    assert(SDDL2_Stack_pop(stack, &v1) == SDDL2_OK);
    assert(SDDL2_Stack_pop(stack, &v2) == SDDL2_OK);
    assert(v1.value.as_i64 == 20);
    assert(v2.value.as_i64 == 10);

    destroy_test_stack(stack);
}

TEST(test_drop_if_leaves_stack_unchanged_when_false)
{
    SDDL2_Stack* stack = create_test_stack(10);

    // Push several values
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(10)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(20)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(30)) == SDDL2_OK); // value
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0))
           == SDDL2_OK); // condition=false

    // Execute drop_if
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_OK);

    // Stack should still have 10, 20, 30
    assert(stack->top == 3);

    SDDL2_Value v1, v2, v3;
    assert(SDDL2_Stack_pop(stack, &v1) == SDDL2_OK);
    assert(SDDL2_Stack_pop(stack, &v2) == SDDL2_OK);
    assert(SDDL2_Stack_pop(stack, &v3) == SDDL2_OK);
    assert(v1.value.as_i64 == 30);
    assert(v2.value.as_i64 == 20);
    assert(v3.value.as_i64 == 10);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Sequential Operations
 * ========================================================================= */

TEST(test_drop_if_sequence)
{
    SDDL2_Stack* stack = create_test_stack(10);

    // Test sequence: drop true, drop false, drop true
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_OK); // drops 1

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(2)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_OK); // keeps 2

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(3)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_OK); // drops 3

    // Only 2 should remain
    assert(stack->top == 1);
    SDDL2_Value result;
    assert(SDDL2_Stack_pop(stack, &result) == SDDL2_OK);
    assert(result.value.as_i64 == 2);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Combined with Other Operations
 * ========================================================================= */

TEST(test_drop_if_with_comparison)
{
    SDDL2_Stack* stack = create_test_stack(10);

    // Pattern: compare two values, drop result based on comparison
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(999))
           == SDDL2_OK); // value to drop
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(10)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(5)) == SDDL2_OK);
    assert(SDDL2_op_gt(stack, NULL, 0) == SDDL2_OK); // 10 > 5 = 1 (true)

    // Should drop 999
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_OK);
    assert(stack->top == 0);

    destroy_test_stack(stack);
}

TEST(test_drop_if_with_arithmetic)
{
    SDDL2_Stack* stack = create_test_stack(10);

    // Use arithmetic result as condition
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK); // value
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(10)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(10)) == SDDL2_OK);
    assert(SDDL2_op_sub(stack, NULL, 0) == SDDL2_OK); // 10 - 10 = 0 (false)

    // Should NOT drop 42
    assert(SDDL2_op_stack_drop_if(stack, NULL, 0) == SDDL2_OK);
    assert(stack->top == 1);

    SDDL2_Value result;
    assert(SDDL2_Stack_pop(stack, &result) == SDDL2_OK);
    assert(result.value.as_i64 == 42);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Test Runner
 * ========================================================================= */

int main(void)
{
    return sddl2_run_all_tests();
}
