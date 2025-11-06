// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Unit tests for OpenZL VM arithmetic operations.
 * Tests Phase 2: Basic Arithmetic
 */

#include "openzl/compress/graphs/sddlv2/sddl2_vm.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/* Test helper: Create a stack for testing */
static openzl_stack* create_test_stack(size_t capacity)
{
    openzl_stack* stack = malloc(sizeof(openzl_stack));
    assert(stack != NULL);
    
    stack->items = malloc(sizeof(openzl_value) * capacity);
    assert(stack->items != NULL);
    
    stack->capacity = capacity;
    SDDL2_stack_init(stack);
    
    return stack;
}

static void destroy_test_stack(openzl_stack* stack)
{
    free(stack->items);
    free(stack);
}

/* ============================================================================
 * Basic Operation Tests
 * ========================================================================= */

static void test_add_basic(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // 5 + 3 = 8
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(5)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(3)) == OPENZL_STACK_OK);
    assert(SDDL2_op_add(stack) == OPENZL_STACK_OK);
    
    openzl_value result;
    assert(SDDL2_stack_pop(stack, &result) == OPENZL_STACK_OK);
    assert(result.kind == OPENZL_VALUE_I64);
    assert(result.value.as_i64 == 8);
    
    destroy_test_stack(stack);
    printf("✓ test_add_basic passed\n");
}

static void test_sub_basic(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // 10 - 4 = 6
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(10)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(4)) == OPENZL_STACK_OK);
    assert(SDDL2_op_sub(stack) == OPENZL_STACK_OK);
    
    openzl_value result;
    assert(SDDL2_stack_pop(stack, &result) == OPENZL_STACK_OK);
    assert(result.kind == OPENZL_VALUE_I64);
    assert(result.value.as_i64 == 6);
    
    destroy_test_stack(stack);
    printf("✓ test_sub_basic passed\n");
}

static void test_mul_basic(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // 7 * 6 = 42
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(7)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(6)) == OPENZL_STACK_OK);
    assert(SDDL2_op_mul(stack) == OPENZL_STACK_OK);
    
    openzl_value result;
    assert(SDDL2_stack_pop(stack, &result) == OPENZL_STACK_OK);
    assert(result.kind == OPENZL_VALUE_I64);
    assert(result.value.as_i64 == 42);
    
    destroy_test_stack(stack);
    printf("✓ test_mul_basic passed\n");
}

static void test_div_basic(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // 20 / 4 = 5
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(20)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(4)) == OPENZL_STACK_OK);
    assert(SDDL2_op_div(stack) == OPENZL_STACK_OK);
    
    openzl_value result;
    assert(SDDL2_stack_pop(stack, &result) == OPENZL_STACK_OK);
    assert(result.kind == OPENZL_VALUE_I64);
    assert(result.value.as_i64 == 5);
    
    destroy_test_stack(stack);
    printf("✓ test_div_basic passed\n");
}

static void test_mod_basic(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // 17 % 5 = 2
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(17)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(5)) == OPENZL_STACK_OK);
    assert(SDDL2_op_mod(stack) == OPENZL_STACK_OK);
    
    openzl_value result;
    assert(SDDL2_stack_pop(stack, &result) == OPENZL_STACK_OK);
    assert(result.kind == OPENZL_VALUE_I64);
    assert(result.value.as_i64 == 2);
    
    destroy_test_stack(stack);
    printf("✓ test_mod_basic passed\n");
}

static void test_abs_basic(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // abs(-42) = 42
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-42)) == OPENZL_STACK_OK);
    assert(SDDL2_op_abs(stack) == OPENZL_STACK_OK);
    
    openzl_value result;
    assert(SDDL2_stack_pop(stack, &result) == OPENZL_STACK_OK);
    assert(result.kind == OPENZL_VALUE_I64);
    assert(result.value.as_i64 == 42);
    
    // abs(42) = 42
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(42)) == OPENZL_STACK_OK);
    assert(SDDL2_op_abs(stack) == OPENZL_STACK_OK);
    assert(SDDL2_stack_pop(stack, &result) == OPENZL_STACK_OK);
    assert(result.value.as_i64 == 42);
    
    destroy_test_stack(stack);
    printf("✓ test_abs_basic passed\n");
}

static void test_neg_basic(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // -(-42) = 42
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-42)) == OPENZL_STACK_OK);
    assert(SDDL2_op_neg(stack) == OPENZL_STACK_OK);
    
    openzl_value result;
    assert(SDDL2_stack_pop(stack, &result) == OPENZL_STACK_OK);
    assert(result.kind == OPENZL_VALUE_I64);
    assert(result.value.as_i64 == 42);
    
    // -(42) = -42
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(42)) == OPENZL_STACK_OK);
    assert(SDDL2_op_neg(stack) == OPENZL_STACK_OK);
    assert(SDDL2_stack_pop(stack, &result) == OPENZL_STACK_OK);
    assert(result.value.as_i64 == -42);
    
    destroy_test_stack(stack);
    printf("✓ test_neg_basic passed\n");
}

/* ============================================================================
 * Overflow Detection Tests
 * ========================================================================= */

static void test_add_overflow(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // INT64_MAX + 1 = overflow
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MAX)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(1)) == OPENZL_STACK_OK);
    assert(SDDL2_op_add(stack) == OPENZL_STACK_OVERFLOW);
    
    // INT64_MIN + (-1) = overflow
    SDDL2_stack_init(stack);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MIN)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-1)) == OPENZL_STACK_OK);
    assert(SDDL2_op_add(stack) == OPENZL_STACK_OVERFLOW);
    
    destroy_test_stack(stack);
    printf("✓ test_add_overflow passed\n");
}

static void test_sub_overflow(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // INT64_MIN - 1 = overflow
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MIN)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(1)) == OPENZL_STACK_OK);
    assert(SDDL2_op_sub(stack) == OPENZL_STACK_OVERFLOW);
    
    // INT64_MAX - (-1) = overflow
    SDDL2_stack_init(stack);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MAX)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-1)) == OPENZL_STACK_OK);
    assert(SDDL2_op_sub(stack) == OPENZL_STACK_OVERFLOW);
    
    destroy_test_stack(stack);
    printf("✓ test_sub_overflow passed\n");
}

static void test_mul_overflow(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // INT64_MAX * 2 = overflow
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MAX)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(2)) == OPENZL_STACK_OK);
    assert(SDDL2_op_mul(stack) == OPENZL_STACK_OVERFLOW);
    
    // INT64_MIN * (-1) = overflow
    SDDL2_stack_init(stack);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MIN)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-1)) == OPENZL_STACK_OK);
    assert(SDDL2_op_mul(stack) == OPENZL_STACK_OVERFLOW);
    
    destroy_test_stack(stack);
    printf("✓ test_mul_overflow passed\n");
}

static void test_abs_overflow(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // abs(INT64_MIN) = overflow
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MIN)) == OPENZL_STACK_OK);
    assert(SDDL2_op_abs(stack) == OPENZL_STACK_OVERFLOW);
    
    destroy_test_stack(stack);
    printf("✓ test_abs_overflow passed\n");
}

static void test_neg_overflow(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // -(INT64_MIN) = overflow
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MIN)) == OPENZL_STACK_OK);
    assert(SDDL2_op_neg(stack) == OPENZL_STACK_OVERFLOW);
    
    destroy_test_stack(stack);
    printf("✓ test_neg_overflow passed\n");
}

/* ============================================================================
 * Divide-by-Zero Tests
 * ========================================================================= */

static void test_div_by_zero(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // 10 / 0 = error
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(10)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(0)) == OPENZL_STACK_OK);
    assert(SDDL2_op_div(stack) == OPENZL_STACK_OVERFLOW);  // Using OVERFLOW for div-by-zero
    
    destroy_test_stack(stack);
    printf("✓ test_div_by_zero passed\n");
}

static void test_mod_by_zero(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // 10 % 0 = error
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(10)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(0)) == OPENZL_STACK_OK);
    assert(SDDL2_op_mod(stack) == OPENZL_STACK_OVERFLOW);  // Using OVERFLOW for div-by-zero
    
    destroy_test_stack(stack);
    printf("✓ test_mod_by_zero passed\n");
}

static void test_div_overflow_special(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // INT64_MIN / -1 = overflow
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(INT64_MIN)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-1)) == OPENZL_STACK_OK);
    assert(SDDL2_op_div(stack) == OPENZL_STACK_OVERFLOW);
    
    destroy_test_stack(stack);
    printf("✓ test_div_overflow_special passed\n");
}

/* ============================================================================
 * Type Mismatch Tests
 * ========================================================================= */

static void test_add_type_mismatch(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // I64 + Tag = type mismatch
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(5)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_tag(100)) == OPENZL_STACK_OK);
    assert(SDDL2_op_add(stack) == OPENZL_STACK_TYPE_MISMATCH);
    
    destroy_test_stack(stack);
    printf("✓ test_add_type_mismatch passed\n");
}

static void test_mul_type_mismatch(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // Tag * I64 = type mismatch
    openzl_type t = { .kind = OPENZL_TYPE_I32LE, .width = 1 };
    assert(SDDL2_stack_push(stack, SDDL2_value_type(t)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(5)) == OPENZL_STACK_OK);
    assert(SDDL2_op_mul(stack) == OPENZL_STACK_TYPE_MISMATCH);
    
    destroy_test_stack(stack);
    printf("✓ test_mul_type_mismatch passed\n");
}

static void test_abs_type_mismatch(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // abs(Tag) = type mismatch
    assert(SDDL2_stack_push(stack, SDDL2_value_tag(42)) == OPENZL_STACK_OK);
    assert(SDDL2_op_abs(stack) == OPENZL_STACK_TYPE_MISMATCH);
    
    destroy_test_stack(stack);
    printf("✓ test_abs_type_mismatch passed\n");
}

/* ============================================================================
 * Stack Underflow Tests
 * ========================================================================= */

static void test_add_underflow(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // Empty stack - should underflow
    assert(SDDL2_op_add(stack) == OPENZL_STACK_UNDERFLOW);
    
    // Only one operand - should underflow
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(5)) == OPENZL_STACK_OK);
    assert(SDDL2_op_add(stack) == OPENZL_STACK_UNDERFLOW);
    
    destroy_test_stack(stack);
    printf("✓ test_add_underflow passed\n");
}

static void test_neg_underflow(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // Empty stack - should underflow
    assert(SDDL2_op_neg(stack) == OPENZL_STACK_UNDERFLOW);
    
    destroy_test_stack(stack);
    printf("✓ test_neg_underflow passed\n");
}

/* ============================================================================
 * Edge Case Tests
 * ========================================================================= */

static void test_zero_operations(void)
{
    openzl_stack* stack = create_test_stack(100);
    openzl_value result;
    
    // 0 + 0 = 0
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(0)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(0)) == OPENZL_STACK_OK);
    assert(SDDL2_op_add(stack) == OPENZL_STACK_OK);
    assert(SDDL2_stack_pop(stack, &result) == OPENZL_STACK_OK);
    assert(result.value.as_i64 == 0);
    
    // 0 * 1000 = 0
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(0)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(1000)) == OPENZL_STACK_OK);
    assert(SDDL2_op_mul(stack) == OPENZL_STACK_OK);
    assert(SDDL2_stack_pop(stack, &result) == OPENZL_STACK_OK);
    assert(result.value.as_i64 == 0);
    
    // abs(0) = 0
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(0)) == OPENZL_STACK_OK);
    assert(SDDL2_op_abs(stack) == OPENZL_STACK_OK);
    assert(SDDL2_stack_pop(stack, &result) == OPENZL_STACK_OK);
    assert(result.value.as_i64 == 0);
    
    destroy_test_stack(stack);
    printf("✓ test_zero_operations passed\n");
}

static void test_negative_operations(void)
{
    openzl_stack* stack = create_test_stack(100);
    openzl_value result;
    
    // -5 + 3 = -2
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-5)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(3)) == OPENZL_STACK_OK);
    assert(SDDL2_op_add(stack) == OPENZL_STACK_OK);
    assert(SDDL2_stack_pop(stack, &result) == OPENZL_STACK_OK);
    assert(result.value.as_i64 == -2);
    
    // -10 / -2 = 5
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-10)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-2)) == OPENZL_STACK_OK);
    assert(SDDL2_op_div(stack) == OPENZL_STACK_OK);
    assert(SDDL2_stack_pop(stack, &result) == OPENZL_STACK_OK);
    assert(result.value.as_i64 == 5);
    
    // -17 % 5 = -2 (C89/C99 behavior)
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-17)) == OPENZL_STACK_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(5)) == OPENZL_STACK_OK);
    assert(SDDL2_op_mod(stack) == OPENZL_STACK_OK);
    assert(SDDL2_stack_pop(stack, &result) == OPENZL_STACK_OK);
    assert(result.value.as_i64 == -2);
    
    destroy_test_stack(stack);
    printf("✓ test_negative_operations passed\n");
}

/* ============================================================================
 * Main Test Runner
 * ========================================================================= */

int main(void)
{
    printf("Running OpenZL VM Phase 2 Tests (Arithmetic)...\n\n");
    
    // Basic operations
    test_add_basic();
    test_sub_basic();
    test_mul_basic();
    test_div_basic();
    test_mod_basic();
    test_abs_basic();
    test_neg_basic();
    
    // Overflow detection
    test_add_overflow();
    test_sub_overflow();
    test_mul_overflow();
    test_abs_overflow();
    test_neg_overflow();
    
    // Divide-by-zero
    test_div_by_zero();
    test_mod_by_zero();
    test_div_overflow_special();
    
    // Type mismatch
    test_add_type_mismatch();
    test_mul_type_mismatch();
    test_abs_type_mismatch();
    
    // Stack underflow
    test_add_underflow();
    test_neg_underflow();
    
    // Edge cases
    test_zero_operations();
    test_negative_operations();
    
    printf("\n✓✓✓ All Phase 2 tests passed! ✓✓✓\n");
    return 0;
}
