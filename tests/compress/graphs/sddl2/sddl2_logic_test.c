// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Comprehensive tests for SDDL2 LOGIC operations
 * Tests all boolean/logical operations (AND, OR, XOR, NOT)
 * All operations treat 0 as false and non-zero as true, returning 0 or 1.
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
 * AND Operation Tests - Logical AND (returns 0 or 1)
 * ========================================================================= */

TEST(test_logic_and_true_true)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: true && true = 1
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_and(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 1);

    destroy_test_stack(stack);
}

TEST(test_logic_and_true_false)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: true && false = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_and(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

TEST(test_logic_and_false_true)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: false && true = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_and(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

TEST(test_logic_and_false_false)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: false && false = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_and(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

TEST(test_logic_and_nonzero_values)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: Any non-zero values are treated as true
    // 100 && 200 = 1
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(100)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(200)) == SDDL2_OK);
    assert(SDDL2_op_and(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 1);

    destroy_test_stack(stack);
}

TEST(test_logic_and_negative_values)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: Negative values are treated as true
    // -1 && -2 = 1
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-2)) == SDDL2_OK);
    assert(SDDL2_op_and(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 1);

    destroy_test_stack(stack);
}

TEST(test_logic_and_zero_with_nonzero)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: 0xFFFFFFFF && 0 = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0xFFFFFFFF)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_and(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

/* ============================================================================
 * OR Operation Tests - Logical OR (returns 0 or 1)
 * ========================================================================= */

TEST(test_logic_or_true_true)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: true || true = 1
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_or(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 1);

    destroy_test_stack(stack);
}

TEST(test_logic_or_true_false)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: true || false = 1
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_or(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 1);

    destroy_test_stack(stack);
}

TEST(test_logic_or_false_true)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: false || true = 1
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_or(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 1);

    destroy_test_stack(stack);
}

TEST(test_logic_or_false_false)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: false || false = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_or(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

TEST(test_logic_or_nonzero_values)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: Any non-zero values are treated as true
    // 100 || 200 = 1
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(100)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(200)) == SDDL2_OK);
    assert(SDDL2_op_or(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 1);

    destroy_test_stack(stack);
}

TEST(test_logic_or_zero_with_nonzero)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: 0 || 42 = 1
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_or(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 1);

    destroy_test_stack(stack);
}

TEST(test_logic_or_negative_values)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: Negative values are treated as true
    // -1 || 0 = 1
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_or(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 1);

    destroy_test_stack(stack);
}

/* ============================================================================
 * XOR Operation Tests - Logical XOR (returns 0 or 1)
 * ========================================================================= */

TEST(test_logic_xor_true_true)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: true ^^ true = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_true_false)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: true ^^ false = 1
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 1);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_false_true)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: false ^^ true = 1
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 1);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_false_false)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: false ^^ false = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_nonzero_values)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: Both non-zero = false
    // 100 ^^ 200 = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(100)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(200)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_zero_with_nonzero)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: One true, one false = true
    // 0 ^^ 42 = 1
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 1);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_negative_values)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: Both negative (both true) = false
    // -1 ^^ -2 = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-2)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

/* ============================================================================
 * NOT Operation Tests - Logical NOT (returns 0 or 1)
 * ========================================================================= */

TEST(test_logic_not_zero)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: !0 = 1
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_not(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 1);

    destroy_test_stack(stack);
}

TEST(test_logic_not_one)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: !1 = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_not(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

TEST(test_logic_not_positive)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: !42 = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_not(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

TEST(test_logic_not_negative)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: !(-1) = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_op_not(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

TEST(test_logic_not_large_value)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: !(large value) = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x123456789ABCDEF0)) == SDDL2_OK);
    assert(SDDL2_op_not(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

TEST(test_logic_not_double_negation)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: !!0 = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_not(stack, NULL, 0) == SDDL2_OK);
    assert(SDDL2_op_not(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

TEST(test_logic_not_double_negation_nonzero)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: !!42 = 1 (normalizes to boolean)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_not(stack, NULL, 0) == SDDL2_OK);
    assert(SDDL2_op_not(stack, NULL, 0) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 1);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Error Condition Tests
 * ========================================================================= */

TEST(test_logic_and_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Empty stack
    assert(SDDL2_op_and(stack, NULL, 0) == SDDL2_STACK_UNDERFLOW);

    // Only one value
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_and(stack, NULL, 0) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_logic_or_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Empty stack
    assert(SDDL2_op_or(stack, NULL, 0) == SDDL2_STACK_UNDERFLOW);

    // Only one value
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_or(stack, NULL, 0) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Empty stack
    assert(SDDL2_op_xor(stack, NULL, 0) == SDDL2_STACK_UNDERFLOW);

    // Only one value
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack, NULL, 0) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_logic_not_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Empty stack
    assert(SDDL2_op_not(stack, NULL, 0) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_logic_and_type_mismatch)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Push Tag instead of I64 for second operand
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(100)) == SDDL2_OK);
    assert(SDDL2_op_and(stack, NULL, 0) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_logic_or_type_mismatch)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Push Type instead of I64 for first operand
    SDDL2_Type type = { .kind = SDDL2_TYPE_U8, .width = 1 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_or(stack, NULL, 0) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_type_mismatch)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Push two Tags instead of I64s
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(10)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(20)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack, NULL, 0) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_logic_not_type_mismatch)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Push Type instead of I64
    SDDL2_Type type = { .kind = SDDL2_TYPE_U8, .width = 1 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_not(stack, NULL, 0) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Combined Operations Tests
 * ========================================================================= */

TEST(test_logic_combined_operations)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: ((a && b) || c) ^^ d
    // a=1, b=0, c=1, d=0
    // a && b = 0
    // (a && b) || c = 1
    // ((a && b) || c) ^^ d = 1

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_and(stack, NULL, 0) == SDDL2_OK); // Result: 0

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_or(stack, NULL, 0) == SDDL2_OK); // Result: 1

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack, NULL, 0) == SDDL2_OK); // Result: 1

    POP_AND_VERIFY_I64(stack, 1);

    destroy_test_stack(stack);
}

TEST(test_logic_de_morgan_law)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test De Morgan's Law: !(a || b) == (!a && !b)
    // a=1, b=0
    // a || b = 1, !(a || b) = 0
    // !a = 0, !b = 1, (!a && !b) = 0

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_or(stack, NULL, 0) == SDDL2_OK); // Result: 1
    assert(SDDL2_op_not(stack, NULL, 0) == SDDL2_OK); // Result: 0

    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

int main(void)
{
    return sddl2_run_all_tests();
}
