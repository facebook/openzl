// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Tests for SDDL2_op_type_structure() VM Operation
 *
 * Tests the stack-based operation for creating structure types.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "openzl/compress/graphs/sddl2/sddl2_vm.h"
#include "sddl2_test_framework.h"

// Stack storage for tests
#define STACK_SIZE 256
static SDDL2_Value g_stack_storage[STACK_SIZE];

/* ============================================================================
 * Test 1: Simple Structure via VM Operation
 * ========================================================================= */

TEST(test_vm_op_simple_structure)
{
    // Create structure {U8, I16LE, I32LE} via stack operations
    SDDL2_Stack stack;
    stack.items    = g_stack_storage;
    stack.capacity = STACK_SIZE;
    SDDL2_Stack_init(&stack);

    // Push member types onto stack (bottom to top: Type₀, Type₁, Type₂)
    SDDL2_Type u8_type  = { SDDL2_TYPE_U8, 1, .struct_data = NULL };
    SDDL2_Type i16_type = { SDDL2_TYPE_I16LE, 1, .struct_data = NULL };
    SDDL2_Type i32_type = { SDDL2_TYPE_I32LE, 1, .struct_data = NULL };

    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(u8_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(i16_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(i32_type)) == SDDL2_OK);

    // Push member count
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(3)) == SDDL2_OK);

    // Execute type.structure operation (use NULL allocator = malloc fallback)
    assert(SDDL2_op_type_structure(&stack, NULL, NULL) == SDDL2_OK);

    // Stack should now have one value: the structure type
    assert(SDDL2_Stack_depth(&stack) == 1);

    // Pop and verify the structure type
    SDDL2_Value result;
    assert(SDDL2_Stack_pop(&stack, &result) == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_TYPE);
    assert(result.value.as_type.kind == SDDL2_TYPE_STRUCTURE);
    assert(result.value.as_type.width == 1); // Single instance
    assert(result.value.as_type.struct_data != NULL);

    // Verify structure data
    SDDL2_Struct_data* struct_data =
            result.value.as_type.struct_data;
    assert(struct_data->member_count == 3);
    assert(struct_data->total_size_bytes == 7); // 1 + 2 + 4

    // Verify members (should be in order: U8, I16LE, I32LE)
    assert(struct_data->members[0].kind == SDDL2_TYPE_U8);
    assert(struct_data->members[1].kind == SDDL2_TYPE_I16LE);
    assert(struct_data->members[2].kind == SDDL2_TYPE_I32LE);

    // Verify total size via SDDL2_Type_size
    assert(SDDL2_Type_size(result.value.as_type) == 7);

    // Cleanup
    free(struct_data);
}

/* ============================================================================
 * Test 2: Structure with Arrays
 * ========================================================================= */

TEST(test_vm_op_structure_with_arrays)
{
    // Create structure {U8, [I32LE × 10], I16LE}
    SDDL2_Stack stack;
    stack.items    = g_stack_storage;
    stack.capacity = STACK_SIZE;
    SDDL2_Stack_init(&stack);

    SDDL2_Type u8_type        = { SDDL2_TYPE_U8, 1, .struct_data = NULL };
    SDDL2_Type i32_array_type = { SDDL2_TYPE_I32LE, 10, .struct_data = NULL }; // Array!
    SDDL2_Type i16_type       = { SDDL2_TYPE_I16LE, 1, .struct_data = NULL };

    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(u8_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(i32_array_type))
           == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(i16_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(3)) == SDDL2_OK);

    assert(SDDL2_op_type_structure(&stack, NULL, NULL) == SDDL2_OK);

    SDDL2_Value result;
    assert(SDDL2_Stack_pop(&stack, &result) == SDDL2_OK);

    SDDL2_Struct_data* struct_data =
            result.value.as_type.struct_data;

    // Size: 1 + 40 + 2 = 43 bytes
    assert(struct_data->total_size_bytes == 43);
    assert(struct_data->members[1].width == 10); // Verify array member

    free(struct_data);
}

/* ============================================================================
 * Test 3: Array of Structures (via type.fixed_array)
 * ========================================================================= */

TEST(test_vm_op_array_of_structures)
{
    // First create a structure {U8, I32LE}
    SDDL2_Stack stack;
    stack.items    = g_stack_storage;
    stack.capacity = STACK_SIZE;
    SDDL2_Stack_init(&stack);

    SDDL2_Type u8_type  = { SDDL2_TYPE_U8, 1, .struct_data = NULL };
    SDDL2_Type i32_type = { SDDL2_TYPE_I32LE, 1, .struct_data = NULL };

    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(u8_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_type(i32_type)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(2)) == SDDL2_OK);

    assert(SDDL2_op_type_structure(&stack, NULL, NULL) == SDDL2_OK);

    // Now we have a structure type on the stack
    // Use type.fixed_array to create an array of 10 such structures
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(10)) == SDDL2_OK);
    assert(SDDL2_op_type_fixed_array(&stack) == SDDL2_OK);

    // Verify the result
    SDDL2_Value result;
    assert(SDDL2_Stack_pop(&stack, &result) == SDDL2_OK);
    assert(result.value.as_type.kind == SDDL2_TYPE_STRUCTURE);
    assert(result.value.as_type.width == 10); // 10 instances!

    SDDL2_Struct_data* struct_data =
            result.value.as_type.struct_data;
    assert(struct_data->total_size_bytes == 5); // Size of one instance

    // Total size = 5 bytes × 10 = 50 bytes
    assert(SDDL2_Type_size(result.value.as_type) == 50);

    free(struct_data);
}

/* ============================================================================
 * Test 4: Zero-Member Structures (for conditional use)
 * ========================================================================= */

TEST(test_vm_op_structure_zero_members)
{
    SDDL2_Stack stack;
    stack.items    = g_stack_storage;
    stack.capacity = STACK_SIZE;
    SDDL2_Stack_init(&stack);

    // Push zero count - should succeed (for conditional structures)
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_type_structure(&stack, NULL, NULL) == SDDL2_OK);

    // Verify result: zero-member structure with size 0
    SDDL2_Value result;
    assert(SDDL2_Stack_pop(&stack, &result) == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_TYPE);
    assert(result.value.as_type.kind == SDDL2_TYPE_STRUCTURE);
    assert(result.value.as_type.width == 1);

    // Verify total size is 0
    SDDL2_Struct_data* struct_data =
            result.value.as_type.struct_data;
    assert(struct_data != NULL);
    assert(struct_data->member_count == 0);
    assert(struct_data->total_size_bytes == 0);
}

/* ============================================================================
 * Test 5: Error Handling - Negative Members
 * ========================================================================= */

TEST(test_vm_op_structure_error_negative_members)
{
    SDDL2_Stack stack;
    stack.items    = g_stack_storage;
    stack.capacity = STACK_SIZE;
    SDDL2_Stack_init(&stack);

    // Push negative count - should fail
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_op_type_structure(&stack, NULL, NULL) == SDDL2_TYPE_MISMATCH);
}

/* ============================================================================
 * Test 6: Error Handling - Wrong Type on Stack
 * ========================================================================= */

TEST(test_vm_op_structure_error_wrong_type)
{
    SDDL2_Stack stack;
    stack.items    = g_stack_storage;
    stack.capacity = STACK_SIZE;
    SDDL2_Stack_init(&stack);

    // Push I64 instead of Type
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(42)) == SDDL2_OK);
    assert(SDDL2_Stack_push(&stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_type_structure(&stack, NULL, NULL) == SDDL2_TYPE_MISMATCH);
}

/* ============================================================================
 * Main Test Runner
 * ========================================================================= */

int main(void)
{
    return sddl2_run_all_tests();
}
