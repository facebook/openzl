// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Tests for type.sizeof operation
 */

#include <limits.h>
#include "sddl2_test_framework.h"

/* ============================================================================
 * Test Cases - Success Scenarios
 * ========================================================================= */

/**
 * Test: sizeof primitive type (I32LE should return 4)
 */
TEST(test_sizeof_primitive_i32le)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Push type I32LE with width=1
    SDDL2_Type type = { .kind = SDDL2_TYPE_I32LE, .width = 1 };
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type));
    assert(err == SDDL2_OK);

    // Get sizeof
    err = SDDL2_op_type_sizeof(&stack);
    assert(err == SDDL2_OK);

    // Verify result
    assert(SDDL2_Stack_depth(&stack) == 1);

    SDDL2_Value result;
    err = SDDL2_Stack_pop(&stack, &result);
    assert(err == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_I64);
    assert(result.value.as_i64 == 4); // I32LE is 4 bytes
}

/**
 * Test: sizeof various primitive types
 */
TEST(test_sizeof_various_primitives)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;

    struct {
        SDDL2_Type_kind kind;
        int64_t expected_size;
    } test_cases[] = {
        { SDDL2_TYPE_U8, 1 },
        { SDDL2_TYPE_I8, 1 },
        { SDDL2_TYPE_BYTES, 1 },
        { SDDL2_TYPE_U16LE, 2 },
        { SDDL2_TYPE_I16BE, 2 },
        { SDDL2_TYPE_F16LE, 2 },
        { SDDL2_TYPE_U32LE, 4 },
        { SDDL2_TYPE_I32BE, 4 },
        { SDDL2_TYPE_F32LE, 4 },
        { SDDL2_TYPE_U64LE, 8 },
        { SDDL2_TYPE_I64BE, 8 },
        { SDDL2_TYPE_F64LE, 8 },
    };

    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        SDDL2_Stack_init(&stack);

        SDDL2_Type type = { .kind = test_cases[i].kind, .width = 1 };
        SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type));
        assert(err == SDDL2_OK);

        err = SDDL2_op_type_sizeof(&stack);
        assert(err == SDDL2_OK);

        SDDL2_Value result;
        err = SDDL2_Stack_pop(&stack, &result);
        assert(err == SDDL2_OK);
        assert(result.kind == SDDL2_VALUE_I64);
        assert(result.value.as_i64 == test_cases[i].expected_size);
    }
}

/**
 * Test: sizeof array type (I16LE[10] should return 20)
 */
TEST(test_sizeof_array_type)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Push type I16LE with width=10 (array of 10)
    SDDL2_Type type = { .kind = SDDL2_TYPE_I16LE, .width = 10 };
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type));
    assert(err == SDDL2_OK);

    // Get sizeof
    err = SDDL2_op_type_sizeof(&stack);
    assert(err == SDDL2_OK);

    // Verify result
    SDDL2_Value result;
    err = SDDL2_Stack_pop(&stack, &result);
    assert(err == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_I64);
    assert(result.value.as_i64 == 20); // 2 bytes * 10 elements
}

/**
 * Test: sizeof with type.fixed_array integration
 */
TEST(test_sizeof_with_fixed_array)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Create array type using type.fixed_array
    SDDL2_Type base_type = { .kind = SDDL2_TYPE_U32LE, .width = 1 };
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(base_type));
    assert(err == SDDL2_OK);

    err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(5));
    assert(err == SDDL2_OK);

    err = SDDL2_op_type_fixed_array(&stack);
    assert(err == SDDL2_OK);

    // Now get its size
    err = SDDL2_op_type_sizeof(&stack);
    assert(err == SDDL2_OK);

    // Verify result: U32LE[5] = 4 * 5 = 20 bytes
    SDDL2_Value result;
    err = SDDL2_Stack_pop(&stack, &result);
    assert(err == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_I64);
    assert(result.value.as_i64 == 20);
}

/**
 * Test: sizeof structure type
 */
TEST(test_sizeof_structure_type)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Build structure: U8, I16LE, I32LE
    // Expected size: 1 + 2 + 4 = 7 bytes
    SDDL2_Type type1 = { .kind = SDDL2_TYPE_U8, .width = 1 };
    SDDL2_Type type2 = { .kind = SDDL2_TYPE_I16LE, .width = 1 };
    SDDL2_Type type3 = { .kind = SDDL2_TYPE_I32LE, .width = 1 };

    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type1));
    assert(err == SDDL2_OK);
    err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type2));
    assert(err == SDDL2_OK);
    err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type3));
    assert(err == SDDL2_OK);

    // Create structure
    err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(3));
    assert(err == SDDL2_OK);

    err = SDDL2_op_type_structure(&stack, NULL, NULL);
    assert(err == SDDL2_OK);

    // Get sizeof
    err = SDDL2_op_type_sizeof(&stack);
    assert(err == SDDL2_OK);

    // Verify result
    SDDL2_Value result;
    err = SDDL2_Stack_pop(&stack, &result);
    assert(err == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_I64);
    assert(result.value.as_i64 == 7); // 1 + 2 + 4
}

/**
 * Test: sizeof structure with array members
 */
TEST(test_sizeof_structure_with_arrays)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Build structure: U8, I16LE[5], I32LE
    // Expected size: 1 + (2*5) + 4 = 15 bytes
    SDDL2_Type type1 = { .kind = SDDL2_TYPE_U8, .width = 1 };
    SDDL2_Type type2 = { .kind = SDDL2_TYPE_I16LE, .width = 5 };
    SDDL2_Type type3 = { .kind = SDDL2_TYPE_I32LE, .width = 1 };

    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type1));
    assert(err == SDDL2_OK);
    err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type2));
    assert(err == SDDL2_OK);
    err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type3));
    assert(err == SDDL2_OK);

    // Create structure
    err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(3));
    assert(err == SDDL2_OK);

    err = SDDL2_op_type_structure(&stack, NULL, NULL);
    assert(err == SDDL2_OK);

    // Get sizeof
    err = SDDL2_op_type_sizeof(&stack);
    assert(err == SDDL2_OK);

    // Verify result
    SDDL2_Value result;
    err = SDDL2_Stack_pop(&stack, &result);
    assert(err == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_I64);
    assert(result.value.as_i64 == 15); // 1 + 10 + 4
}

/**
 * Test: sizeof large array
 */
TEST(test_sizeof_large_array)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // U64LE[1000] = 8 * 1000 = 8000 bytes
    SDDL2_Type type = { .kind = SDDL2_TYPE_U64LE, .width = 1000 };
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type));
    assert(err == SDDL2_OK);

    err = SDDL2_op_type_sizeof(&stack);
    assert(err == SDDL2_OK);

    SDDL2_Value result;
    err = SDDL2_Stack_pop(&stack, &result);
    assert(err == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_I64);
    assert(result.value.as_i64 == 8000);
}

/**
 * Test: sizeof bytes type with width
 */
TEST(test_sizeof_bytes_array)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // BYTES[100] = 1 * 100 = 100 bytes
    SDDL2_Type type = { .kind = SDDL2_TYPE_BYTES, .width = 100 };
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type));
    assert(err == SDDL2_OK);

    err = SDDL2_op_type_sizeof(&stack);
    assert(err == SDDL2_OK);

    SDDL2_Value result;
    err = SDDL2_Stack_pop(&stack, &result);
    assert(err == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_I64);
    assert(result.value.as_i64 == 100);
}

/* ============================================================================
 * Test Cases - Error Scenarios
 * ========================================================================= */

/**
 * Test: Error - empty stack (stack underflow)
 */
TEST(test_error_empty_stack)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Empty stack
    assert(SDDL2_Stack_depth(&stack) == 0);

    // Try to get sizeof (should fail)
    SDDL2_Error err = SDDL2_op_type_sizeof(&stack);
    assert(err == SDDL2_STACK_UNDERFLOW);

    // Stack should still be empty
    assert(SDDL2_Stack_depth(&stack) == 0);
}

/**
 * Test: Error - top element is I64, not Type
 */
TEST(test_error_top_is_i64)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Push I64 value
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(42));
    assert(err == SDDL2_OK);

    // Try to get sizeof (should fail)
    err = SDDL2_op_type_sizeof(&stack);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Stack should be empty (value was popped before error detected)
    assert(SDDL2_Stack_depth(&stack) == 0);
}

/**
 * Test: Error - top element is Tag, not Type
 */
TEST(test_error_top_is_tag)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Push Tag value
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_tag(100));
    assert(err == SDDL2_OK);

    // Try to get sizeof (should fail)
    err = SDDL2_op_type_sizeof(&stack);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Stack should be empty (value was popped before error detected)
    assert(SDDL2_Stack_depth(&stack) == 0);
}

/**
 * Test: Error - multiple values on stack, top is wrong type
 */
TEST(test_error_multiple_values_top_wrong)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Push valid Type
    SDDL2_Type type = { .kind = SDDL2_TYPE_U32LE, .width = 1 };
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type));
    assert(err == SDDL2_OK);

    // Push I64 on top
    err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(99));
    assert(err == SDDL2_OK);

    assert(SDDL2_Stack_depth(&stack) == 2);

    // Try to get sizeof (should fail because top is I64)
    err = SDDL2_op_type_sizeof(&stack);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Stack should have 1 element left (Type remains)
    assert(SDDL2_Stack_depth(&stack) == 1);
}

/* ============================================================================
 * Test Cases - Integration Scenarios
 * ========================================================================= */

/**
 * Test: Use sizeof for validation (like in sao_full.asm)
 */
TEST(test_sizeof_for_validation)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Build a structure
    SDDL2_Type type1 = { .kind = SDDL2_TYPE_F64LE, .width = 1 };  // 8 bytes
    SDDL2_Type type2 = { .kind = SDDL2_TYPE_F64LE, .width = 1 };  // 8 bytes
    SDDL2_Type type3 = { .kind = SDDL2_TYPE_BYTES, .width = 2 };  // 2 bytes
    // Total: 18 bytes

    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type1));
    assert(err == SDDL2_OK);
    err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type2));
    assert(err == SDDL2_OK);
    err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type3));
    assert(err == SDDL2_OK);

    err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(3));
    assert(err == SDDL2_OK);

    err = SDDL2_op_type_structure(&stack, NULL, NULL);
    assert(err == SDDL2_OK);

    // Duplicate the type for validation
    err = SDDL2_op_dup(&stack);
    assert(err == SDDL2_OK);

    // Get sizeof
    err = SDDL2_op_type_sizeof(&stack);
    assert(err == SDDL2_OK);

    // Check it equals expected value (18)
    SDDL2_Value size_val;
    err = SDDL2_Stack_pop(&stack, &size_val);
    assert(err == SDDL2_OK);
    assert(size_val.value.as_i64 == 18);

    // Original type should still be on stack
    assert(SDDL2_Stack_depth(&stack) == 1);
}

/**
 * Test: sizeof preserves stack below
 */
TEST(test_sizeof_preserves_stack_below)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Push some values first
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(100));
    assert(err == SDDL2_OK);
    err = SDDL2_Stack_push(&stack, SDDL2_Value_i64(200));
    assert(err == SDDL2_OK);

    // Push a type
    SDDL2_Type type = { .kind = SDDL2_TYPE_I32LE, .width = 1 };
    err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type));
    assert(err == SDDL2_OK);

    assert(SDDL2_Stack_depth(&stack) == 3);

    // Get sizeof
    err = SDDL2_op_type_sizeof(&stack);
    assert(err == SDDL2_OK);

    // Should have 3 elements: I64(100), I64(200), I64(4)
    assert(SDDL2_Stack_depth(&stack) == 3);

    SDDL2_Value val;
    err = SDDL2_Stack_pop(&stack, &val);
    assert(err == SDDL2_OK);
    assert(val.kind == SDDL2_VALUE_I64);
    assert(val.value.as_i64 == 4);

    err = SDDL2_Stack_pop(&stack, &val);
    assert(err == SDDL2_OK);
    assert(val.kind == SDDL2_VALUE_I64);
    assert(val.value.as_i64 == 200);

    err = SDDL2_Stack_pop(&stack, &val);
    assert(err == SDDL2_OK);
    assert(val.kind == SDDL2_VALUE_I64);
    assert(val.value.as_i64 == 100);
}

/**
 * Test: sizeof can be called multiple times
 */
TEST(test_sizeof_multiple_times)
{
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    // Create type once
    SDDL2_Type type = { .kind = SDDL2_TYPE_U64LE, .width = 5 };
    SDDL2_Error err = SDDL2_Stack_push(&stack, SDDL2_Value_type(type));
    assert(err == SDDL2_OK);

    // Duplicate it
    err = SDDL2_op_dup(&stack);
    assert(err == SDDL2_OK);

    // Get sizeof first copy
    err = SDDL2_op_type_sizeof(&stack);
    assert(err == SDDL2_OK);

    SDDL2_Value result1;
    err = SDDL2_Stack_pop(&stack, &result1);
    assert(err == SDDL2_OK);
    assert(result1.value.as_i64 == 40); // 8 * 5

    // Get sizeof second copy
    err = SDDL2_op_type_sizeof(&stack);
    assert(err == SDDL2_OK);

    SDDL2_Value result2;
    err = SDDL2_Stack_pop(&stack, &result2);
    assert(err == SDDL2_OK);
    assert(result2.value.as_i64 == 40); // Same result
}

/* ============================================================================
 * Test Runner
 * ========================================================================= */

int main(void)
{
    return sddl2_run_all_tests();
}
