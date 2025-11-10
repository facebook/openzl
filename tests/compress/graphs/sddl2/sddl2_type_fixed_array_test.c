// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Tests for type.fixed_array operation
 */

#include <limits.h>
#include "sddl2_test_framework.h"

/* ============================================================================
 * Test Cases
 * ========================================================================= */

/**
 * Test: Basic array type creation
 */
TEST(test_basic_array_type)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Push base type: U32LE with width=1
    SDDL2_Type base_type = { .kind = SDDL2_TYPE_U32LE, .width = 1 };
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(base_type));
    assert(err == SDDL2_OK);

    // Push array count (10) as I64
    err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(10));
    assert(err == SDDL2_OK);

    // Create array of 10 elements
    err = SDDL2_op_type_fixed_array(&stack);
    assert(err == SDDL2_OK);

    // Verify result
    assert(SDDL2_Stack_depth(&stack) == 1);

    SDDL2_Value result;
    err = SDDL2_Stack_pop(&stack, &result);
    assert(err == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_TYPE);
    assert(result.value.as_type.kind == SDDL2_TYPE_U32LE);
    assert(result.value.as_type.width == 10);
}

/**
 * Test: Array of arrays (nested arrays)
 */
TEST(test_nested_arrays)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Push base type: I16LE with width=1
    SDDL2_Type base_type = { .kind = SDDL2_TYPE_I16LE, .width = 1 };
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(base_type));
    assert(err == SDDL2_OK);

    // Push array count (5) as I64
    err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(5));
    assert(err == SDDL2_OK);

    // Create array of 5 elements: I16LE[5]
    err = SDDL2_op_type_fixed_array(&stack);
    assert(err == SDDL2_OK);

    // Push array count (3) as I64
    err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(3));
    assert(err == SDDL2_OK);

    // Create array of 3 of those: I16LE[15]
    err = SDDL2_op_type_fixed_array(&stack);
    assert(err == SDDL2_OK);

    // Verify result
    SDDL2_Value result;
    err = SDDL2_Stack_pop(&stack, &result);
    assert(err == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_TYPE);
    assert(result.value.as_type.kind == SDDL2_TYPE_I16LE);
    assert(result.value.as_type.width == 15); // 5 * 3
}

/**
 * Test: Array count of 1 (identity operation)
 */
TEST(test_array_count_one)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Push base type: F32BE with width=7
    SDDL2_Type base_type = { .kind = SDDL2_TYPE_F32BE, .width = 7 };
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(base_type));
    assert(err == SDDL2_OK);

    // Push array count (1) as I64
    err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(1));
    assert(err == SDDL2_OK);

    // Create array of 1 element (should preserve width)
    err = SDDL2_op_type_fixed_array(&stack);
    assert(err == SDDL2_OK);

    // Verify result
    SDDL2_Value result;
    err = SDDL2_Stack_pop(&stack, &result);
    assert(err == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_TYPE);
    assert(result.value.as_type.kind == SDDL2_TYPE_F32BE);
    assert(result.value.as_type.width == 7); // 7 * 1
}

/**
 * Test: Error - array count is zero
 */
TEST(test_error_zero_count)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Push base type
    SDDL2_Type base_type = { .kind = SDDL2_TYPE_U8, .width = 1 };
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(base_type));
    assert(err == SDDL2_OK);

    // Push array count (0) as I64
    err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(0));
    assert(err == SDDL2_OK);

    // Try to create array of 0 elements (should fail)
    err = SDDL2_op_type_fixed_array(&stack);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Stack should have base_type left (array_count was popped, validated,
    // failed)
    assert(SDDL2_Stack_depth(&stack) == 1);
}

/**
 * Test: Error - overflow in width multiplication
 */
TEST(test_error_width_overflow)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Push base type with large width
    SDDL2_Type base_type = { .kind  = SDDL2_TYPE_U32LE,
                             .width = UINT32_MAX / 2 };
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(base_type));
    assert(err == SDDL2_OK);

    // Push array count (3) as I64
    err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(3));
    assert(err == SDDL2_OK);

    // Try to multiply by 3 (would overflow: (UINT32_MAX/2) * 3 > UINT32_MAX)
    err = SDDL2_op_type_fixed_array(&stack);
    assert(err == SDDL2_STACK_OVERFLOW);

    // Stack should be empty (both values were popped before overflow detected)
    assert(SDDL2_Stack_depth(&stack) == 0);
}

/**
 * Test: Error - overflow at exact boundary
 */
TEST(test_error_width_overflow_boundary)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Push base type: width that would exactly overflow
    // UINT32_MAX = 4294967295
    // If we multiply UINT32_MAX by 2, that would overflow
    SDDL2_Type base_type = { .kind = SDDL2_TYPE_U8, .width = UINT32_MAX };
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(base_type));
    assert(err == SDDL2_OK);

    // Push array count (2) as I64
    err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(2));
    assert(err == SDDL2_OK);

    // Try to multiply by 2 (would overflow)
    err = SDDL2_op_type_fixed_array(&stack);
    assert(err == SDDL2_STACK_OVERFLOW);
}

/**
 * Test: Success at maximum safe value
 */
TEST(test_max_safe_value)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Push base type with width=1
    SDDL2_Type base_type = { .kind = SDDL2_TYPE_I8, .width = 1 };
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(base_type));
    assert(err == SDDL2_OK);

    // Push array count (UINT32_MAX) as I64
    err = SDDL2_Stack_push(&stack, SDDL2_Value_i64((int64_t)UINT32_MAX));
    assert(err == SDDL2_OK);

    // Create array with UINT32_MAX elements (should succeed)
    err = SDDL2_op_type_fixed_array(&stack);
    assert(err == SDDL2_OK);

    // Verify result
    SDDL2_Value result;
    err = SDDL2_Stack_pop(&stack, &result);
    assert(err == SDDL2_OK);
    assert(result.value.as_type.width == UINT32_MAX);
}

/**
 * Test: Error - wrong type on stack (I64 instead of Type)
 */
TEST(test_error_wrong_type)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Push I64 value instead of Type
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(42));
    assert(err == SDDL2_OK);

    // Push array count (10) as I64
    err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(10));
    assert(err == SDDL2_OK);

    // Try to create array (should fail due to type mismatch)
    err = SDDL2_op_type_fixed_array(&stack);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Stack should be empty (both values were popped)
    assert(SDDL2_Stack_depth(&stack) == 0);
}

/**
 * Test: Error - stack underflow
 */
TEST(test_error_stack_underflow)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Empty stack
    assert(SDDL2_Stack_depth(&stack) == 0);

    // Try to create array (should fail due to underflow)
    SDDL2_Error err = SDDL2_op_type_fixed_array(&stack);
    assert(err == SDDL2_STACK_UNDERFLOW);
}

/**
 * Test: Works with all type kinds
 */
TEST(test_all_type_kinds)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;

    // Test a representative sample of type kinds
    SDDL2_Type_kind types[] = {
        SDDL2_TYPE_BYTES, SDDL2_TYPE_U8,    SDDL2_TYPE_I16LE,  SDDL2_TYPE_U32BE,
        SDDL2_TYPE_I64LE, SDDL2_TYPE_F32LE, SDDL2_TYPE_BF16BE,
    };

    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        SDDL2_Stack_init(&stack);

        SDDL2_Type base_type = { .kind = types[i], .width = 2 };
        SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(base_type));
        assert(err == SDDL2_OK);

        // Push array count (5) as I64
        err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(5));
        assert(err == SDDL2_OK);

        err = SDDL2_op_type_fixed_array(&stack);
        assert(err == SDDL2_OK);

        SDDL2_Value result;
        err = SDDL2_Stack_pop(&stack, &result);
        assert(err == SDDL2_OK);
        assert(result.value.as_type.kind == types[i]);
        assert(result.value.as_type.width == 10); // 2 * 5
    }
}

/* ============================================================================
 * Test Runner
 * ========================================================================= */

int main(void)
{
    return sddl2_run_all_tests();
}
