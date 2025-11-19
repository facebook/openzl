// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Unit tests for OpenZL VM input buffer operations.
 * Tests Phase 3: Input Buffer
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "openzl/compress/graphs/sddl2/sddl2_vm.h"

/* Test helper: Create a stack for testing */
static SDDL2_Stack* create_test_stack(size_t capacity)
{
    SDDL2_Stack* stack = malloc(sizeof(SDDL2_Stack));
    assert(stack != NULL);

    stack->items = malloc(sizeof(SDDL2_Value) * capacity);
    assert(stack != NULL);

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
 * Input Buffer Initialization Tests
 * ========================================================================= */

static void test_buffer_init(void)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 5);

    assert(buffer.data == data);
    assert(buffer.size == 5);
    assert(buffer.current_pos == 0);

    printf("✓ test_buffer_init passed\n");
}

/* ============================================================================
 * current_pos Operation Tests
 * ========================================================================= */

static void test_current_pos_at_start(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0xAA, 0xBB, 0xCC };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 3);

    // Get current_pos (should be 0)
    assert(SDDL2_op_current_pos(stack, &buffer) == SDDL2_OK);

    SDDL2_Value result;
    assert(SDDL2_Stack_pop(stack, &result) == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_I64);
    assert(result.value.as_i64 == 0);

    destroy_test_stack(stack);
    printf("✓ test_current_pos_at_start passed\n");
}

static void test_current_pos_after_advance(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 5);

    // Manually advance cursor (simulating segment creation)
    buffer.current_pos = 3;

    // Get current_pos (should be 3)
    assert(SDDL2_op_current_pos(stack, &buffer) == SDDL2_OK);

    SDDL2_Value result;
    assert(SDDL2_Stack_pop(stack, &result) == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_I64);
    assert(result.value.as_i64 == 3);

    destroy_test_stack(stack);
    printf("✓ test_current_pos_after_advance passed\n");
}

static void test_current_pos_no_advance(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02 };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    buffer.current_pos = 1;

    // Call current_pos multiple times - should not change cursor
    assert(SDDL2_op_current_pos(stack, &buffer) == SDDL2_OK);
    assert(buffer.current_pos == 1); // Should still be 1

    assert(SDDL2_op_current_pos(stack, &buffer) == SDDL2_OK);
    assert(buffer.current_pos == 1); // Should still be 1

    // Stack should have two copies of position
    SDDL2_Value v1, v2;
    assert(SDDL2_Stack_pop(stack, &v2) == SDDL2_OK);
    assert(SDDL2_Stack_pop(stack, &v1) == SDDL2_OK);
    assert(v1.value.as_i64 == 1);
    assert(v2.value.as_i64 == 1);

    destroy_test_stack(stack);
    printf("✓ test_current_pos_no_advance passed\n");
}

/* ============================================================================
 * load.u8 Operation Tests
 * ========================================================================= */

static void test_load_u8_basic(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0xAA, 0xBB, 0xCC, 0xDD };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 4);

    // Load byte at address 0 (should be 0xAA)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_OK);

    SDDL2_Value result;
    assert(SDDL2_Stack_pop(stack, &result) == SDDL2_OK);
    assert(result.kind == SDDL2_VALUE_I64);
    assert(result.value.as_i64 == 0xAA);

    // Load byte at address 2 (should be 0xCC)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(2)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_OK);
    assert(SDDL2_Stack_pop(stack, &result) == SDDL2_OK);
    assert(result.value.as_i64 == 0xCC);

    destroy_test_stack(stack);
    printf("✓ test_load_u8_basic passed\n");
}

static void test_load_u8_all_bytes(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x00, 0xFF, 0x42, 0x7F, 0x80 };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 5);

    // Load all bytes
    for (size_t i = 0; i < 5; i++) {
        assert(SDDL2_Stack_push(stack, SDDL2_Value_i64((int64_t)i))
               == SDDL2_OK);
        assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_OK);

        SDDL2_Value result;
        assert(SDDL2_Stack_pop(stack, &result) == SDDL2_OK);
        assert(result.value.as_i64 == (int64_t)data[i]);
    }

    destroy_test_stack(stack);
    printf("✓ test_load_u8_all_bytes passed\n");
}

static void test_load_u8_no_cursor_advance(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x11, 0x22, 0x33 };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 3);
    size_t original_pos = buffer.current_pos;

    // Load byte - should NOT advance cursor
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_OK);

    assert(buffer.current_pos == original_pos); // Should not have changed

    SDDL2_Value result;
    assert(SDDL2_Stack_pop(stack, &result) == SDDL2_OK);
    assert(result.value.as_i64 == 0x22);

    destroy_test_stack(stack);
    printf("✓ test_load_u8_no_cursor_advance passed\n");
}

/* ============================================================================
 * Bounds Checking Tests
 * ========================================================================= */

static void test_load_u8_out_of_bounds_positive(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03 };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 3);

    // Try to load at address 3 (out of bounds, valid range is 0-2)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(3)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    // Try to load at address 100 (way out of bounds)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(100)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
    printf("✓ test_load_u8_out_of_bounds_positive passed\n");
}

static void test_load_u8_out_of_bounds_negative(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0xAA, 0xBB };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 2);

    // Try to load at negative address
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-100)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
    printf("✓ test_load_u8_out_of_bounds_negative passed\n");
}

static void test_load_u8_at_last_valid_address(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x10, 0x20, 0x30, 0x40, 0x50 };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 5);

    // Load at last valid address (4)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(4)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_OK);

    SDDL2_Value result;
    assert(SDDL2_Stack_pop(stack, &result) == SDDL2_OK);
    assert(result.value.as_i64 == 0x50);

    // Load at address 5 should fail
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(5)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
    printf("✓ test_load_u8_at_last_valid_address passed\n");
}

static void test_load_u8_empty_buffer(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = {}; // Empty buffer
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 0);

    // Any address should fail on empty buffer
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
    printf("✓ test_load_u8_empty_buffer passed\n");
}

/* ============================================================================
 * Type Mismatch Tests
 * ========================================================================= */

static void test_load_u8_type_mismatch(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0xAA, 0xBB };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 2);

    // Try to load with Tag instead of I64
    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(42)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    // Try to load with Type instead of I64
    SDDL2_Type t = { .kind = SDDL2_TYPE_U8, .width = 1 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(t)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
    printf("✓ test_load_u8_type_mismatch passed\n");
}

/* ============================================================================
 * Stack Underflow Tests
 * ========================================================================= */

static void test_load_u8_underflow(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0xAA };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 1);

    // Try to load with empty stack
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
    printf("✓ test_load_u8_underflow passed\n");
}

/* ============================================================================
 * Combined Operation Tests
 * ========================================================================= */

static void test_current_pos_and_load(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x10, 0x20, 0x30, 0x40 };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 4);
    buffer.current_pos = 2; // Simulate being midway through

    // Get current position
    assert(SDDL2_op_current_pos(stack, &buffer) == SDDL2_OK);

    // Use it to load the byte at current position
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_OK);

    SDDL2_Value result;
    assert(SDDL2_Stack_pop(stack, &result) == SDDL2_OK);
    assert(result.value.as_i64 == 0x30); // data[2]

    // Position should still be 2
    assert(buffer.current_pos == 2);

    destroy_test_stack(stack);
    printf("✓ test_current_pos_and_load passed\n");
}

static void test_arithmetic_with_load(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x05, 0x03, 0x08 };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 3);

    // Load data[0] = 5
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_OK);

    // Load data[1] = 3
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_OK);

    // Add them
    assert(SDDL2_op_add(stack, NULL, 0) == SDDL2_OK);

    // Result should be 8
    SDDL2_Value result;
    assert(SDDL2_Stack_pop(stack, &result) == SDDL2_OK);
    assert(result.value.as_i64 == 8);

    // Verify it matches data[2]
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(2)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_OK);
    assert(SDDL2_Stack_pop(stack, &result) == SDDL2_OK);
    assert(result.value.as_i64 == 8);

    destroy_test_stack(stack);
    printf("✓ test_arithmetic_with_load passed\n");
}

/* ============================================================================
 * Main Test Runner
 * ========================================================================= */

int main(void)
{
    printf("Running OpenZL VM Phase 3 Tests (Input Buffer)...\n\n");

    // Buffer initialization
    test_buffer_init();

    // current_pos tests
    test_current_pos_at_start();
    test_current_pos_after_advance();
    test_current_pos_no_advance();

    // load.u8 basic tests
    test_load_u8_basic();
    test_load_u8_all_bytes();
    test_load_u8_no_cursor_advance();

    // Bounds checking
    test_load_u8_out_of_bounds_positive();
    test_load_u8_out_of_bounds_negative();
    test_load_u8_at_last_valid_address();
    test_load_u8_empty_buffer();

    // Type mismatch
    test_load_u8_type_mismatch();

    // Stack underflow
    test_load_u8_underflow();

    // Combined operations
    test_current_pos_and_load();
    test_arithmetic_with_load();

    printf("\n✓✓✓ All Phase 3 tests passed! ✓✓✓\n");
    return 0;
}
