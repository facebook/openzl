// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Basic unit tests for OpenZL VM stack operations.
 * Tests Phase 1: Foundation (Stack + Values)
 */

#include "openzl/compress/graphs/sddlv2/sddl2_vm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * Helper function to create a stack with arena allocation.
 * For testing purposes, we use malloc. In production, this would use
 * the VM's arena allocator.
 */
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

static void test_stack_init(void)
{
    openzl_stack* stack = create_test_stack(OPENZL_STACK_DEPTH_DEFAULT);
    
    assert(SDDL2_stack_depth(stack) == 0);
    assert(SDDL2_stack_is_empty(stack));
    assert(stack->capacity == OPENZL_STACK_DEPTH_DEFAULT);
    
    destroy_test_stack(stack);
    printf("✓ test_stack_init passed\n");
}

static void test_stack_push_pop(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    // Push an I64 value
    openzl_value v1 = SDDL2_value_i64(42);
    assert(SDDL2_stack_push(stack, v1) == OPENZL_STACK_OK);
    assert(SDDL2_stack_depth(stack) == 1);
    assert(!SDDL2_stack_is_empty(stack));
    
    // Push a Tag value
    openzl_value v2 = SDDL2_value_tag(100);
    assert(SDDL2_stack_push(stack, v2) == OPENZL_STACK_OK);
    assert(SDDL2_stack_depth(stack) == 2);
    
    // Pop and verify Tag
    openzl_value popped;
    assert(SDDL2_stack_pop(stack, &popped) == OPENZL_STACK_OK);
    assert(popped.kind == OPENZL_VALUE_TAG);
    assert(popped.value.as_tag == 100);
    assert(SDDL2_stack_depth(stack) == 1);
    
    // Pop and verify I64
    assert(SDDL2_stack_pop(stack, &popped) == OPENZL_STACK_OK);
    assert(popped.kind == OPENZL_VALUE_I64);
    assert(popped.value.as_i64 == 42);
    assert(SDDL2_stack_depth(stack) == 0);
    
    destroy_test_stack(stack);
    printf("✓ test_stack_push_pop passed\n");
}

static void test_stack_underflow(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    openzl_value v;
    assert(SDDL2_stack_pop(stack, &v) == OPENZL_STACK_UNDERFLOW);
    assert(SDDL2_stack_peek(stack, &v) == OPENZL_STACK_UNDERFLOW);
    
    destroy_test_stack(stack);
    printf("✓ test_stack_underflow passed\n");
}

static void test_stack_overflow(void)
{
    // Create a small stack to test overflow
    size_t small_capacity = 10;
    openzl_stack* stack = create_test_stack(small_capacity);
    
    // Fill stack to max capacity
    openzl_value v = SDDL2_value_i64(1);
    for (size_t i = 0; i < small_capacity; i++) {
        assert(SDDL2_stack_push(stack, v) == OPENZL_STACK_OK);
    }
    
    // Try to push one more - should overflow
    assert(SDDL2_stack_push(stack, v) == OPENZL_STACK_OVERFLOW);
    assert(SDDL2_stack_depth(stack) == small_capacity);
    
    destroy_test_stack(stack);
    printf("✓ test_stack_overflow passed\n");
}

static void test_stack_peek(void)
{
    openzl_stack* stack = create_test_stack(100);
    
    openzl_value v1 = SDDL2_value_i64(123);
    assert(SDDL2_stack_push(stack, v1) == OPENZL_STACK_OK);
    
    // Peek should not modify stack
    openzl_value peeked;
    assert(SDDL2_stack_peek(stack, &peeked) == OPENZL_STACK_OK);
    assert(peeked.kind == OPENZL_VALUE_I64);
    assert(peeked.value.as_i64 == 123);
    assert(SDDL2_stack_depth(stack) == 1); // Still 1
    
    // Peek again - should return same value
    assert(SDDL2_stack_peek(stack, &peeked) == OPENZL_STACK_OK);
    assert(peeked.value.as_i64 == 123);
    
    destroy_test_stack(stack);
    printf("✓ test_stack_peek passed\n");
}

static void test_value_kinds(void)
{
    // Test I64 value
    openzl_value v_i64 = SDDL2_value_i64(-9223372036854775807LL);
    assert(v_i64.kind == OPENZL_VALUE_I64);
    assert(v_i64.value.as_i64 == -9223372036854775807LL);
    
    // Test Tag value
    openzl_value v_tag = SDDL2_value_tag(0xDEADBEEF);
    assert(v_tag.kind == OPENZL_VALUE_TAG);
    assert(v_tag.value.as_tag == 0xDEADBEEF);
    
    // Test Type value
    openzl_type t = { .kind = OPENZL_TYPE_I32LE, .width = 4 };
    openzl_value v_type = SDDL2_value_type(t);
    assert(v_type.kind == OPENZL_VALUE_TYPE);
    assert(v_type.value.as_type.kind == OPENZL_TYPE_I32LE);
    assert(v_type.value.as_type.width == 4);
    
    printf("✓ test_value_kinds passed\n");
}

static void test_type_sizes(void)
{
    assert(SDDL2_type_size(OPENZL_TYPE_U8) == 1);
    assert(SDDL2_type_size(OPENZL_TYPE_I8) == 1);
    assert(SDDL2_type_size(OPENZL_TYPE_U16LE) == 2);
    assert(SDDL2_type_size(OPENZL_TYPE_I16BE) == 2);
    assert(SDDL2_type_size(OPENZL_TYPE_U32LE) == 4);
    assert(SDDL2_type_size(OPENZL_TYPE_F32BE) == 4);
    assert(SDDL2_type_size(OPENZL_TYPE_I64LE) == 8);
    assert(SDDL2_type_size(OPENZL_TYPE_F64BE) == 8);
    assert(SDDL2_type_size(OPENZL_TYPE_BYTES) == 1);  // Raw bytes, unit size is 1
    
    printf("✓ test_type_sizes passed\n");
}

int main(void)
{
    printf("Running OpenZL VM Phase 1 Tests...\n\n");
    
    test_stack_init();
    test_stack_push_pop();
    test_stack_underflow();
    test_stack_overflow();
    test_stack_peek();
    test_value_kinds();
    test_type_sizes();
    
    printf("\n✓✓✓ All Phase 1 tests passed! ✓✓✓\n");
    return 0;
}
