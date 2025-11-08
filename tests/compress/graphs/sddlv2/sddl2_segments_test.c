// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Unit tests for OpenZL VM segment generation.
 * Tests Phase 4: Simple Byte Segments
 *
 * This tests the CORE VM FUNCTIONALITY - generating segments from input!
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "openzl/compress/graphs/sddlv2/sddl2_vm.h"

/* Test helpers */
static SDDL2_stack* create_test_stack(size_t capacity)
{
    SDDL2_stack* stack = malloc(sizeof(SDDL2_stack));
    assert(stack != NULL);

    stack->items = malloc(sizeof(SDDL2_value) * capacity);
    assert(stack->items != NULL);

    stack->capacity = capacity;
    SDDL2_stack_init(stack);

    return stack;
}

static void destroy_test_stack(SDDL2_stack* stack)
{
    free(stack->items);
    free(stack);
}

/* ============================================================================
 * Segment List Init/Destroy Tests
 * ========================================================================= */

static void test_segment_list_init(void)
{
    SDDL2_segment_list list;

    SDDL2_segment_list_init(&list, NULL, NULL);

    assert(list.items == NULL);
    assert(list.count == 0);
    assert(list.capacity == 0);

    printf("✓ test_segment_list_init passed\n");
}

static void test_segment_list_destroy(void)
{
    SDDL2_segment_list list;

    SDDL2_segment_list_init(&list, NULL, NULL);
    SDDL2_segment_list_destroy(&list);

    assert(list.items == NULL);
    assert(list.count == 0);
    assert(list.capacity == 0);

    printf("✓ test_segment_list_destroy passed\n");
}

/* ============================================================================
 * Single Segment Tests
 * ========================================================================= */

static void test_create_single_segment(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;

    SDDL2_input_buffer_init(&buffer, data, 4);
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Create unspecified segment: size=4 (no tag)
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(4)) == SDDL2_OK);

    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_OK);

    // Verify segment (tag=0 for unspecified)
    assert(segments.count == 1);
    assert(segments.items[0].tag == 0);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 4);

    // Verify cursor advanced
    assert(buffer.current_pos == 4);

    // Verify stack is empty
    assert(SDDL2_stack_depth(stack) == 0);

    SDDL2_segment_list_destroy(&segments);
    destroy_test_stack(stack);
    printf("✓ test_create_single_segment passed\n");
}

static void test_create_zero_size_segment(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0xAA, 0xBB };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;

    SDDL2_input_buffer_init(&buffer, data, 2);
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Create zero-size unspecified segment: size=0 (no tag)
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(0)) == SDDL2_OK);

    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_OK);

    // Verify segment (tag=0 for unspecified)
    assert(segments.count == 1);
    assert(segments.items[0].tag == 0);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 0);

    // Cursor should not advance
    assert(buffer.current_pos == 0);

    SDDL2_segment_list_destroy(&segments);
    destroy_test_stack(stack);
    printf("✓ test_create_zero_size_segment passed\n");
}

/* ============================================================================
 * Multiple Segments Tests
 * ========================================================================= */

static void test_create_multiple_segments(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;

    SDDL2_input_buffer_init(&buffer, data, 6);
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Segment 1: size=2 (bytes 0-1, unspecified tag=0)
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(2)) == SDDL2_OK);
    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_OK);

    // Segment 2: size=3 (bytes 2-4, unspecified tag=0)
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(3)) == SDDL2_OK);
    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_OK);

    // Segment 3: size=1 (byte 5, unspecified tag=0)
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_OK);

    // Verify all segments
    assert(segments.count == 3);

    assert(segments.items[0].tag == 0); // Unspecified
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 2);

    assert(segments.items[1].tag == 0); // Unspecified
    assert(segments.items[1].start_pos == 2);
    assert(segments.items[1].size_bytes == 3);

    assert(segments.items[2].tag == 0); // Unspecified
    assert(segments.items[2].start_pos == 5);
    assert(segments.items[2].size_bytes == 1);

    // Verify cursor at end
    assert(buffer.current_pos == 6);

    SDDL2_segment_list_destroy(&segments);
    destroy_test_stack(stack);
    printf("✓ test_create_multiple_segments passed\n");
}

static void test_many_segments_growth(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[100];
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;

    for (size_t i = 0; i < 100; i++) {
        data[i] = (uint8_t)i;
    }

    SDDL2_input_buffer_init(&buffer, data, 100);
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Create 50 unspecified segments of size 2 each
    for (int i = 0; i < 50; i++) {
        assert(SDDL2_stack_push(stack, SDDL2_value_i64(2)) == SDDL2_OK);
        assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
               == SDDL2_OK);
    }

    // Verify all 50 segments
    assert(segments.count == 50);

    for (size_t i = 0; i < 50; i++) {
        assert(segments.items[i].tag == 0); // All unspecified
        assert(segments.items[i].start_pos == i * 2);
        assert(segments.items[i].size_bytes == 2);
    }

    // Verify cursor at end
    assert(buffer.current_pos == 100);

    // Verify capacity grew (started at 0, should be >= 50)
    assert(segments.capacity >= 50);

    SDDL2_segment_list_destroy(&segments);
    destroy_test_stack(stack);
    printf("✓ test_many_segments_growth passed\n");
}

/* ============================================================================
 * Bounds Checking Tests
 * ========================================================================= */

static void test_segment_exceeds_buffer(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;

    SDDL2_input_buffer_init(&buffer, data, 3);
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Try to create segment larger than buffer: size=10
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(10)) == SDDL2_OK);

    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_SEGMENT_BOUNDS);

    // No segment should be created
    assert(segments.count == 0);

    // Cursor should not advance
    assert(buffer.current_pos == 0);

    SDDL2_segment_list_destroy(&segments);
    destroy_test_stack(stack);
    printf("✓ test_segment_exceeds_buffer passed\n");
}

static void test_segment_at_exact_boundary(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x10, 0x20, 0x30, 0x40, 0x50 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;

    SDDL2_input_buffer_init(&buffer, data, 5);
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Create segment exactly at buffer size: size=5
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(5)) == SDDL2_OK);

    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_OK);

    // Verify segment
    assert(segments.count == 1);
    assert(segments.items[0].size_bytes == 5);
    assert(buffer.current_pos == 5);

    // Try to create one more byte - should fail
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_SEGMENT_BOUNDS);

    SDDL2_segment_list_destroy(&segments);
    destroy_test_stack(stack);
    printf("✓ test_segment_at_exact_boundary passed\n");
}

static void test_segment_after_partial_consumption(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0xAA, 0xBB, 0xCC, 0xDD };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;

    SDDL2_input_buffer_init(&buffer, data, 4);
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Create first segment: size=2
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(2)) == SDDL2_OK);
    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_OK);

    // Try to create segment that's too large for remaining: size=3 (only 2
    // left)
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(3)) == SDDL2_OK);
    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_SEGMENT_BOUNDS);

    // Only first segment should exist
    assert(segments.count == 1);
    assert(buffer.current_pos == 2);

    SDDL2_segment_list_destroy(&segments);
    destroy_test_stack(stack);
    printf("✓ test_segment_after_partial_consumption passed\n");
}

/* ============================================================================
 * Type Mismatch Tests
 * ========================================================================= */

static void test_segment_wrong_tag_type(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;

    SDDL2_input_buffer_init(&buffer, data, 2);
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Push Tag instead of I64 for size
    assert(SDDL2_stack_push(stack, SDDL2_value_tag(100)) == SDDL2_OK);

    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_TYPE_MISMATCH);

    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);
    destroy_test_stack(stack);
    printf("✓ test_segment_wrong_tag_type passed\n");
}

static void test_segment_wrong_size_type(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;

    SDDL2_input_buffer_init(&buffer, data, 2);
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Push Type instead of I64 for size
    SDDL2_type t = { .kind = SDDL2_TYPE_U8, .width = 1 };
    assert(SDDL2_stack_push(stack, SDDL2_value_type(t)) == SDDL2_OK);

    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_TYPE_MISMATCH);

    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);
    destroy_test_stack(stack);
    printf("✓ test_segment_wrong_size_type passed\n");
}

static void test_segment_negative_tag(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;

    SDDL2_input_buffer_init(&buffer, data, 2);
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Negative size should fail
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-100)) == SDDL2_OK);

    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_TYPE_MISMATCH);

    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);
    destroy_test_stack(stack);
    printf("✓ test_segment_negative_tag passed\n"); // Test name kept for now
}

static void test_segment_negative_size(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;

    SDDL2_input_buffer_init(&buffer, data, 2);
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Another negative size test
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(-5)) == SDDL2_OK);

    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_TYPE_MISMATCH);

    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);
    destroy_test_stack(stack);
    printf("✓ test_segment_negative_size passed\n");
}

/* ============================================================================
 * Stack Underflow Tests
 * ========================================================================= */

static void test_segment_empty_stack(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;

    SDDL2_input_buffer_init(&buffer, data, 2);
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Empty stack - should underflow
    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_STACK_UNDERFLOW);

    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);
    destroy_test_stack(stack);
    printf("✓ test_segment_empty_stack passed\n");
}

static void test_segment_missing_size(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;

    SDDL2_input_buffer_init(&buffer, data, 2);
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Don't push anything - test empty stack

    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_STACK_UNDERFLOW);

    assert(segments.count == 0);

    SDDL2_segment_list_destroy(&segments);
    destroy_test_stack(stack);
    printf("✓ test_segment_missing_size passed\n"); // Now tests empty stack
}

/* ============================================================================
 * Integration Tests
 * ========================================================================= */

static void test_end_to_end_simple_program(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x48, 0x65, 0x6C, 0x6C, 0x6F }; // "Hello"
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;

    SDDL2_input_buffer_init(&buffer, data, 5);
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Simulate VM program:
    // push 5            # size
    // segment_create_unspecified

    assert(SDDL2_stack_push(stack, SDDL2_value_i64(5)) == SDDL2_OK);
    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_OK);

    // Result: One unspecified segment covering entire "Hello" string
    assert(segments.count == 1);
    assert(segments.items[0].tag == 0); // Unspecified
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 5);
    assert(buffer.current_pos == 5);

    SDDL2_segment_list_destroy(&segments);
    destroy_test_stack(stack);
    printf("✓ test_end_to_end_simple_program passed\n");
}

static void test_segments_with_arithmetic(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[10]   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;

    SDDL2_input_buffer_init(&buffer, data, 10);
    SDDL2_segment_list_init(&segments, NULL, NULL);

    // Simulate VM program with arithmetic:
    // push 2            # a
    // push 3            # b
    // add               # a + b = 5
    // segment_create_unspecified

    assert(SDDL2_stack_push(stack, SDDL2_value_i64(2)) == SDDL2_OK);
    assert(SDDL2_stack_push(stack, SDDL2_value_i64(3)) == SDDL2_OK);
    assert(SDDL2_op_add(stack) == SDDL2_OK);
    assert(SDDL2_op_segment_create_unspecified(stack, &buffer, &segments)
           == SDDL2_OK);

    // Result: One segment of size 5
    assert(segments.count == 1);
    assert(segments.items[0].size_bytes == 5);

    SDDL2_segment_list_destroy(&segments);
    destroy_test_stack(stack);
    printf("✓ test_segments_with_arithmetic passed\n");
}

/* ============================================================================
 * Main Test Runner
 * ========================================================================= */

int main(void)
{
    printf("Running OpenZL VM Phase 4 Tests (Simple Byte Segments)...\n\n");

    // Init/destroy
    test_segment_list_init();
    test_segment_list_destroy();

    // Single segment
    test_create_single_segment();
    test_create_zero_size_segment();

    // Multiple segments
    test_create_multiple_segments();
    test_many_segments_growth();

    // Bounds checking
    test_segment_exceeds_buffer();
    test_segment_at_exact_boundary();
    test_segment_after_partial_consumption();

    // Type mismatch
    test_segment_wrong_tag_type();
    test_segment_wrong_size_type();
    test_segment_negative_tag();
    test_segment_negative_size();

    // Stack underflow
    test_segment_empty_stack();
    test_segment_missing_size();

    // Integration
    test_end_to_end_simple_program();
    test_segments_with_arithmetic();

    printf("\n✓✓✓ All Phase 4 tests passed! ✓✓✓\n");
    printf("\n🎉 END-TO-END SEGMENT GENERATION WORKING! 🎉\n");
    return 0;
}
