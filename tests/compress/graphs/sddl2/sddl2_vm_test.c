// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Basic unit tests for OpenZL VM stack operations.
 * Tests Phase 1: Foundation (Stack + Values)
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "openzl/compress/graphs/sddl2/sddl2_vm.h"
#include "sddl2_test_framework.h"

/* Test helper: Create a stack for testing */
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

TEST(test_stack_init)
{
    SDDL2_Stack* stack = create_test_stack(SDDL2_STACK_DEPTH_DEFAULT);

    // Verify initialization
    assert(stack != NULL);
    assert(stack->capacity == SDDL2_STACK_DEPTH_DEFAULT);

    destroy_test_stack(stack);
}

TEST(test_stack_push_pop)
{
    SDDL2_Stack* stack = create_test_stack(100);

    // Push an I64 value
    SDDL2_Value v1 = SDDL2_Value_i64(42);
    assert(SDDL2_Stack_push(stack, v1) == SDDL2_OK);
    assert(SDDL2_Stack_depth(stack) == 1);
    assert(!SDDL2_Stack_is_empty(stack));

    // Push a Tag value
    SDDL2_Value v2 = SDDL2_Value_tag(100);
    assert(SDDL2_Stack_push(stack, v2) == SDDL2_OK);
    assert(SDDL2_Stack_depth(stack) == 2);

    // Pop and verify Tag
    SDDL2_Value popped;
    assert(SDDL2_Stack_pop(stack, &popped) == SDDL2_OK);
    assert(popped.kind == SDDL2_VALUE_TAG);
    assert(popped.value.as_tag == 100);
    assert(SDDL2_Stack_depth(stack) == 1);

    // Pop and verify I64
    assert(SDDL2_Stack_pop(stack, &popped) == SDDL2_OK);
    assert(popped.kind == SDDL2_VALUE_I64);
    assert(popped.value.as_i64 == 42);
    assert(SDDL2_Stack_depth(stack) == 0);

    destroy_test_stack(stack);
}

TEST(test_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);

    SDDL2_Value v;
    assert(SDDL2_Stack_pop(stack, &v) == SDDL2_STACK_UNDERFLOW);
    assert(SDDL2_Stack_peek(stack, &v) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_stack_overflow)
{
    // Create a small stack to test overflow
    size_t small_capacity = 10;
    SDDL2_Stack* stack    = create_test_stack(small_capacity);

    // Fill stack to max capacity
    SDDL2_Value v = SDDL2_Value_i64(1);
    for (size_t i = 0; i < small_capacity; i++) {
        assert(SDDL2_Stack_push(stack, v) == SDDL2_OK);
    }

    // Try to push one more - should overflow
    assert(SDDL2_Stack_push(stack, v) == SDDL2_STACK_OVERFLOW);
    assert(SDDL2_Stack_depth(stack) == small_capacity);

    destroy_test_stack(stack);
}

TEST(test_stack_peek)
{
    SDDL2_Stack* stack = create_test_stack(100);

    SDDL2_Value v1 = SDDL2_Value_i64(123);
    assert(SDDL2_Stack_push(stack, v1) == SDDL2_OK);

    // Peek should not modify stack
    SDDL2_Value peeked;
    assert(SDDL2_Stack_peek(stack, &peeked) == SDDL2_OK);
    assert(peeked.kind == SDDL2_VALUE_I64);
    assert(peeked.value.as_i64 == 123);
    assert(SDDL2_Stack_depth(stack) == 1); // Still 1

    // Peek again - should return same value
    assert(SDDL2_Stack_peek(stack, &peeked) == SDDL2_OK);
    assert(peeked.value.as_i64 == 123);

    destroy_test_stack(stack);
}

TEST(test_value_kinds)
{
    // Test I64 value
    SDDL2_Value v_i64 = SDDL2_Value_i64(-9223372036854775807LL);
    assert(v_i64.kind == SDDL2_VALUE_I64);
    assert(v_i64.value.as_i64 == -9223372036854775807LL);

    // Test Tag value
    SDDL2_Value v_tag = SDDL2_Value_tag(0xDEADBEEF);
    assert(v_tag.kind == SDDL2_VALUE_TAG);
    assert(v_tag.value.as_tag == 0xDEADBEEF);

    // Test Type value
    SDDL2_Type t       = { .kind = SDDL2_TYPE_I32LE, .width = 4 };
    SDDL2_Value v_type = SDDL2_Value_type(t);
    assert(v_type.kind == SDDL2_VALUE_TYPE);
    assert(v_type.value.as_type.kind == SDDL2_TYPE_I32LE);
    assert(v_type.value.as_type.width == 4);
}

TEST(test_type_sizes)
{
    assert(SDDL2_kind_size(SDDL2_TYPE_U8) == 1);
    assert(SDDL2_kind_size(SDDL2_TYPE_I8) == 1);
    assert(SDDL2_kind_size(SDDL2_TYPE_U16LE) == 2);
    assert(SDDL2_kind_size(SDDL2_TYPE_I16BE) == 2);
    assert(SDDL2_kind_size(SDDL2_TYPE_U32LE) == 4);
    assert(SDDL2_kind_size(SDDL2_TYPE_F32BE) == 4);
    assert(SDDL2_kind_size(SDDL2_TYPE_I64LE) == 8);
    assert(SDDL2_kind_size(SDDL2_TYPE_F64BE) == 8);
    assert(SDDL2_kind_size(SDDL2_TYPE_BYTES) == 1); // Raw bytes, unit size is 1
}

int main(void)
{
    return sddl2_run_all_tests();
}
