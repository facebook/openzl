// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Comprehensive tests for SDDL2 LOGIC operations
 * Tests all bitwise logical operations (AND, OR, XOR, NOT)
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
 * AND Operation Tests
 * ========================================================================= */

TEST(test_logic_and_basic)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: 0xFF00 & 0x0FF0 = 0x0F00
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0xFF00)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x0FF0)) == SDDL2_OK);
    assert(SDDL2_op_and(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0x0F00);

    destroy_test_stack(stack);
}

TEST(test_logic_and_all_ones)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: -1 & -1 = -1 (all bits set)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_op_and(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, -1);

    destroy_test_stack(stack);
}

TEST(test_logic_and_with_zero)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: 0xFFFFFFFF & 0 = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0xFFFFFFFF)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_and(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

TEST(test_logic_and_negative_values)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: -1 & 0x7FFFFFFF = 0x7FFFFFFF (mask off sign bit)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x7FFFFFFF)) == SDDL2_OK);
    assert(SDDL2_op_and(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0x7FFFFFFF);

    destroy_test_stack(stack);
}

/* ============================================================================
 * OR Operation Tests
 * ========================================================================= */

TEST(test_logic_or_basic)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: 0x00F0 | 0x0F00 = 0x0FF0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x00F0)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x0F00)) == SDDL2_OK);
    assert(SDDL2_op_or(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0x0FF0);

    destroy_test_stack(stack);
}

TEST(test_logic_or_with_zero)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: 0x1234 | 0 = 0x1234
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x1234)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_or(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0x1234);

    destroy_test_stack(stack);
}

TEST(test_logic_or_all_ones)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: 0x1234 | -1 = -1
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x1234)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_op_or(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, -1);

    destroy_test_stack(stack);
}

TEST(test_logic_or_negative_values)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: -2 | 1 = -1 (set lowest bit)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-2)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_or(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, -1);

    destroy_test_stack(stack);
}

/* ============================================================================
 * XOR Operation Tests
 * ========================================================================= */

TEST(test_logic_xor_basic)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: 0xAAAA ^ 0x5555 = 0xFFFF
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0xAAAA)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x5555)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0xFFFF);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_with_self)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: value ^ value = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x12345678)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x12345678)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_with_zero)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: value ^ 0 = value
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x1234)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0x1234);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_bit_flip)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: 0xFF00 ^ 0xFFFF = 0x00FF (flip lower byte)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0xFF00)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0xFFFF)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0x00FF);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_negative_values)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: -1 ^ -1 = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

/* ============================================================================
 * NOT Operation Tests
 * ========================================================================= */

TEST(test_logic_not_basic)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: ~0x0F0F = 0xFFFFFFFFFFFFF0F0 (all bits flipped)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x0F0F)) == SDDL2_OK);
    assert(SDDL2_op_not(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, (int64_t)0xFFFFFFFFFFFFF0F0);

    destroy_test_stack(stack);
}

TEST(test_logic_not_zero)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: ~0 = -1 (all bits set)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_not(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, -1);

    destroy_test_stack(stack);
}

TEST(test_logic_not_all_ones)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: ~(-1) = 0
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_op_not(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0);

    destroy_test_stack(stack);
}

TEST(test_logic_not_double_negation)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: ~~value = value
    int64_t value = 0x123456789ABCDEF0;
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(value)) == SDDL2_OK);
    assert(SDDL2_op_not(stack) == SDDL2_OK);
    assert(SDDL2_op_not(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, value);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Error Condition Tests
 * ========================================================================= */

TEST(test_logic_and_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Empty stack
    assert(SDDL2_op_and(stack) == SDDL2_STACK_UNDERFLOW);

    // Only one value
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_and(stack) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_logic_or_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Empty stack
    assert(SDDL2_op_or(stack) == SDDL2_STACK_UNDERFLOW);

    // Only one value
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_or(stack) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Empty stack
    assert(SDDL2_op_xor(stack) == SDDL2_STACK_UNDERFLOW);

    // Only one value
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_logic_not_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Empty stack
    assert(SDDL2_op_not(stack) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_logic_and_type_mismatch)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Push Tag instead of I64 for second operand
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(100)) == SDDL2_OK);
    assert(SDDL2_op_and(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_logic_and_type_mismatch_both_tags)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Both operands are Tags instead of I64
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(50)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(100)) == SDDL2_OK);
    assert(SDDL2_op_and(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_logic_and_type_mismatch_both_types)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Both operands are Types instead of I64
    SDDL2_Type type1 = { .kind = SDDL2_TYPE_U8, .width = 1 };
    SDDL2_Type type2 = { .kind = SDDL2_TYPE_U16LE, .width = 2 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type2)) == SDDL2_OK);
    assert(SDDL2_op_and(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_logic_and_type_mismatch_tag_and_type)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // One Tag and one Type
    SDDL2_Type type = { .kind = SDDL2_TYPE_U32LE, .width = 4 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(123)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_and(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_logic_or_type_mismatch)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Push Type instead of I64 for first operand
    SDDL2_Type type = { .kind = SDDL2_TYPE_U8, .width = 1 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_op_or(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_logic_or_type_mismatch_both_tags)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Both operands are Tags instead of I64
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(77)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(88)) == SDDL2_OK);
    assert(SDDL2_op_or(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_logic_or_type_mismatch_both_types)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Both operands are Types instead of I64
    SDDL2_Type type1 = { .kind = SDDL2_TYPE_I32LE, .width = 4 };
    SDDL2_Type type2 = { .kind = SDDL2_TYPE_I64LE, .width = 8 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type2)) == SDDL2_OK);
    assert(SDDL2_op_or(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_logic_or_type_mismatch_tag_and_type)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // One Type and one Tag
    SDDL2_Type type = { .kind = SDDL2_TYPE_U64LE, .width = 8 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(999)) == SDDL2_OK);
    assert(SDDL2_op_or(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_type_mismatch)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Push two Tags instead of I64s
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(10)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(20)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_type_mismatch_type_and_i64)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // One Type and one I64
    SDDL2_Type type = { .kind = SDDL2_TYPE_I16LE, .width = 2 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x1234)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_type_mismatch_both_types)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Both operands are Types instead of I64
    SDDL2_Type type1 = { .kind = SDDL2_TYPE_U8, .width = 1 };
    SDDL2_Type type2 = { .kind = SDDL2_TYPE_U32LE, .width = 4 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type1)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type2)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_logic_xor_type_mismatch_tag_and_type)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // One Tag and one Type
    SDDL2_Type type = { .kind = SDDL2_TYPE_I8, .width = 1 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(555)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_logic_not_type_mismatch)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Push Type instead of I64
    SDDL2_Type type = { .kind = SDDL2_TYPE_U8, .width = 1 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_not(stack) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Combined Operations Tests
 * ========================================================================= */

TEST(test_logic_combined_operations)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Test: ((a AND b) OR c) XOR d
    // a = 0xFF00, b = 0x0FF0, c = 0x000F, d = 0xFFFF
    // a AND b = 0x0F00
    // (a AND b) OR c = 0x0F0F
    // ((a AND b) OR c) XOR d = 0xF0F0

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0xFF00)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x0FF0)) == SDDL2_OK);
    assert(SDDL2_op_and(stack) == SDDL2_OK); // Result: 0x0F00

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0x000F)) == SDDL2_OK);
    assert(SDDL2_op_or(stack) == SDDL2_OK); // Result: 0x0F0F

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0xFFFF)) == SDDL2_OK);
    assert(SDDL2_op_xor(stack) == SDDL2_OK); // Result: 0xF0F0

    POP_AND_VERIFY_I64(stack, 0xF0F0);

    destroy_test_stack(stack);
}

TEST(test_logic_bit_masking)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Extract bits 8-15 from 0x12345678
    // Step 1: value & 0xFF00 = 0x5600
    // Step 2: result >> 8 would be done with other ops, but we test masking
    int64_t value = 0x12345678;
    int64_t mask  = 0xFF00;

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(value)) == SDDL2_OK);
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(mask)) == SDDL2_OK);
    assert(SDDL2_op_and(stack) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0x5600);

    destroy_test_stack(stack);
}

int main(void)
{
    return sddl2_run_all_tests();
}
