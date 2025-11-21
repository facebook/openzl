// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Comprehensive tests for SDDL2 push.stack_depth operation
 * Tests the stack introspection opcode
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

static int64_t pop_i64(SDDL2_Stack* stack)
{
    SDDL2_Value val;
    assert(SDDL2_Stack_pop(stack, &val) == SDDL2_OK);
    assert(val.kind == SDDL2_VALUE_I64);
    return val.value.as_i64;
}

/* ============================================================================
 * push.stack_depth Basic Tests
 * ========================================================================= */

TEST(test_push_stack_depth_empty_stack)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: push.stack_depth on empty stack should push 0
    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(SDDL2_Stack_depth(stack) == 1);
    assert(pop_i64(stack) == 0);

    destroy_test_stack(stack);
}

TEST(test_push_stack_depth_one_element)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: push.stack_depth with one element on stack should push 1
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(SDDL2_Stack_depth(stack) == 2);
    assert(pop_i64(stack) == 1);

    destroy_test_stack(stack);
}

TEST(test_push_stack_depth_multiple_elements)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: push.stack_depth with 5 elements on stack should push 5
    for (int i = 0; i < 5; i++) {
        assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(i)) == SDDL2_OK);
    }
    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(SDDL2_Stack_depth(stack) == 6);
    assert(pop_i64(stack) == 5);

    destroy_test_stack(stack);
}

TEST(test_push_stack_depth_large_depth)
{
    SDDL2_Stack* stack = create_test_stack(1000);

    // Test: push.stack_depth with 100 elements
    for (int i = 0; i < 100; i++) {
        assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(i)) == SDDL2_OK);
    }
    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(SDDL2_Stack_depth(stack) == 101);
    assert(pop_i64(stack) == 100);

    destroy_test_stack(stack);
}

/* ============================================================================
 * push.stack_depth Interaction Tests
 * ========================================================================= */

TEST(test_push_stack_depth_after_push)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: Verify depth increases after pushes
    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(pop_i64(stack) == 0);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(10)) == SDDL2_OK);
    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(pop_i64(stack) == 1);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(20)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(30)) == SDDL2_OK);
    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(pop_i64(stack) == 3);

    destroy_test_stack(stack);
}

TEST(test_push_stack_depth_after_pop)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: Verify depth decreases after pops
    for (int i = 0; i < 5; i++) {
        assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(i)) == SDDL2_OK);
    }

    SDDL2_Value val;
    assert(SDDL2_Stack_pop(stack, &val) == SDDL2_OK);
    assert(SDDL2_Stack_pop(stack, &val) == SDDL2_OK);

    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(pop_i64(stack) == 3);

    destroy_test_stack(stack);
}

TEST(test_push_stack_depth_with_different_types)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: push.stack_depth counts all stack elements regardless of type
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(100)) == SDDL2_OK);
    SDDL2_Type type = { .kind = SDDL2_TYPE_U32LE, .width = 1 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);

    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(SDDL2_Stack_depth(stack) == 4);
    assert(pop_i64(stack) == 3);

    destroy_test_stack(stack);
}

/* ============================================================================
 * push.stack_depth Overflow Test
 * ========================================================================= */

TEST(test_push_stack_depth_near_capacity)
{
    SDDL2_Stack* stack = create_test_stack(10);

    // Test: push.stack_depth when stack is nearly full
    for (int i = 0; i < 9; i++) {
        assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(i)) == SDDL2_OK);
    }

    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(SDDL2_Stack_depth(stack) == 10);
    assert(pop_i64(stack) == 9);

    destroy_test_stack(stack);
}

TEST(test_push_stack_depth_overflow)
{
    SDDL2_Stack* stack = create_test_stack(10);

    // Test: push.stack_depth fails with STACK_OVERFLOW when stack is full
    for (int i = 0; i < 10; i++) {
        assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(i)) == SDDL2_OK);
    }

    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_STACK_OVERFLOW);
    assert(SDDL2_Stack_depth(stack) == 10);

    destroy_test_stack(stack);
}

/* ============================================================================
 * push.stack_depth Combined Operations Tests
 * ========================================================================= */

TEST(test_push_stack_depth_with_arithmetic)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: Use stack depth in arithmetic operations
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(10)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(20)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(30)) == SDDL2_OK);

    // Get depth (3) and multiply by 10
    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(10)) == SDDL2_OK);
    assert(SDDL2_op_mul(stack, NULL, 0) == SDDL2_OK);
    assert(pop_i64(stack) == 30); // 3 * 10

    destroy_test_stack(stack);
}

TEST(test_push_stack_depth_with_comparison)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: Compare stack depth against expected value
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(2)) == SDDL2_OK);

    // Check if depth equals 2
    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(2)) == SDDL2_OK);
    assert(SDDL2_op_eq(stack, NULL, 0) == SDDL2_OK);
    assert(pop_i64(stack) == 1); // True

    destroy_test_stack(stack);
}

TEST(test_push_stack_depth_validation_pattern)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: Validation pattern - ensure exactly 3 elements on stack
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(10)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(20)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(30)) == SDDL2_OK);

    // Validate depth == 3
    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(3)) == SDDL2_OK);
    assert(SDDL2_op_eq(stack, NULL, 0) == SDDL2_OK);
    assert(SDDL2_op_expect_true(stack, NULL) == SDDL2_OK);

    destroy_test_stack(stack);
}

TEST(test_push_stack_depth_multiple_calls)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: Multiple calls to push.stack_depth
    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(pop_i64(stack) == 0);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    // After operations: [1, depth(1)=1, depth(2)=2] = 3 elements
    assert(SDDL2_Stack_depth(stack) == 3);
    assert(pop_i64(stack) == 2);
    assert(pop_i64(stack) == 1);

    destroy_test_stack(stack);
}

/* ============================================================================
 * push.stack_depth Edge Cases
 * ========================================================================= */

TEST(test_push_stack_depth_after_dup)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: Depth after dup operation
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_dup(stack, NULL, 0) == SDDL2_OK);
    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(pop_i64(stack) == 2);

    destroy_test_stack(stack);
}

TEST(test_push_stack_depth_after_swap)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: Depth unchanged after swap
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(2)) == SDDL2_OK);
    assert(SDDL2_op_swap(stack, NULL, 0) == SDDL2_OK);
    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(pop_i64(stack) == 2);

    destroy_test_stack(stack);
}

TEST(test_push_stack_depth_after_drop)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: Depth decreases after drop
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(2)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(3)) == SDDL2_OK);
    assert(SDDL2_op_drop(stack, NULL, 0) == SDDL2_OK);
    assert(SDDL2_op_push_stack_depth(stack) == SDDL2_OK);
    assert(pop_i64(stack) == 2);

    destroy_test_stack(stack);
}

int main(void)
{
    return sddl2_run_all_tests();
}
