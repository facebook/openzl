// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Unit tests for OpenZL VM tagged segments with automatic merging.
 * Tests Phase 5: Tag Registry & Segment Merging
 *
 * This tests tagged segments and the automatic merging behavior:
 * - When consecutive segments have the same tag, they merge automatically
 * - When tags differ or positions are not consecutive, new segments are created
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
 * Tag Registry Init/Destroy Tests
 * ========================================================================= */

static void test_tag_registry_init(void)
{
    SDDL2_tag_registry registry;

    SDDL2_tag_registry_init(&registry);

    assert(registry.tags == NULL);
    assert(registry.count == 0);
    assert(registry.capacity == 0);

    printf("✓ test_tag_registry_init passed\n");
}

static void test_tag_registry_destroy(void)
{
    SDDL2_tag_registry registry;

    SDDL2_tag_registry_init(&registry);
    SDDL2_tag_registry_destroy(&registry);

    assert(registry.tags == NULL);
    assert(registry.count == 0);
    assert(registry.capacity == 0);

    printf("✓ test_tag_registry_destroy passed\n");
}

/* ============================================================================
 * Single Tagged Segment Tests
 * ========================================================================= */

static void test_create_single_tagged_segment(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;
    SDDL2_tag_registry registry;

    SDDL2_input_buffer_init(&buffer, data, 5);
    SDDL2_segment_list_init(&segments);
    SDDL2_tag_registry_init(&registry);

    // Stack: tag=100, size=5
    SDDL2_stack_push(stack, SDDL2_value_tag(100)); // tag
    SDDL2_stack_push(stack, SDDL2_value_i64(5));   // size

    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    // Check segment
    assert(segments.count == 1);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 5);

    // Check buffer advanced
    assert(buffer.current_pos == 5);

    // Check tag registered
    assert(registry.count == 1);
    assert(registry.tags[0] == 100);

    // Cleanup
    SDDL2_segment_list_destroy(&segments);
    SDDL2_tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_create_single_tagged_segment passed\n");
}

static void test_tagged_segment_zero_size(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;
    SDDL2_tag_registry registry;

    SDDL2_input_buffer_init(&buffer, data, 3);
    SDDL2_segment_list_init(&segments);
    SDDL2_tag_registry_init(&registry);

    // Stack: tag=42, size=0
    SDDL2_stack_push(stack, SDDL2_value_tag(42)); // tag
    SDDL2_stack_push(stack, SDDL2_value_i64(0));  // size

    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    assert(segments.count == 1);
    assert(segments.items[0].tag == 42);
    assert(segments.items[0].size_bytes == 0);
    assert(buffer.current_pos == 0);

    // Cleanup
    SDDL2_segment_list_destroy(&segments);
    SDDL2_tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_segment_zero_size passed\n");
}

/* ============================================================================
 * Automatic Merging Tests (Core Feature)
 * ========================================================================= */

static void test_merge_consecutive_same_tag(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 1, 2, 3, 4, 5, 6, 7, 8 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;
    SDDL2_tag_registry registry;

    SDDL2_input_buffer_init(&buffer, data, 8);
    SDDL2_segment_list_init(&segments);
    SDDL2_tag_registry_init(&registry);

    // Create first segment: tag=100, size=2
    SDDL2_stack_push(stack, SDDL2_value_tag(100));
    SDDL2_stack_push(stack, SDDL2_value_i64(2));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    // Check: 1 segment, size=2
    assert(segments.count == 1);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 2);
    assert(buffer.current_pos == 2);

    // Create second segment: tag=100, size=3 (SHOULD MERGE!)
    SDDL2_stack_push(stack, SDDL2_value_tag(100));
    SDDL2_stack_push(stack, SDDL2_value_i64(3));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    err = SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    // Check: STILL 1 segment, size=5 (merged!)
    assert(segments.count == 1);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 5); // 2 + 3 = 5!
    assert(buffer.current_pos == 5);

    // Cleanup
    SDDL2_segment_list_destroy(&segments);
    SDDL2_tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_merge_consecutive_same_tag passed\n");
}

static void test_no_merge_different_tag(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 1, 2, 3, 4, 5, 6 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;
    SDDL2_tag_registry registry;

    SDDL2_input_buffer_init(&buffer, data, 6);
    SDDL2_segment_list_init(&segments);
    SDDL2_tag_registry_init(&registry);

    // Create first segment: tag=100, size=2
    SDDL2_stack_push(stack, SDDL2_value_tag(100));
    SDDL2_stack_push(stack, SDDL2_value_i64(2));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    // Create second segment: tag=200, size=3 (different tag, NO MERGE)
    SDDL2_stack_push(stack, SDDL2_value_tag(200));
    SDDL2_stack_push(stack, SDDL2_value_i64(3));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    err = SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    // Check: 2 separate segments
    assert(segments.count == 2);

    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 2);

    assert(segments.items[1].tag == 200);
    assert(segments.items[1].start_pos == 2);
    assert(segments.items[1].size_bytes == 3);

    assert(buffer.current_pos == 5);

    // Check both tags registered
    assert(registry.count == 2);

    // Cleanup
    SDDL2_segment_list_destroy(&segments);
    SDDL2_tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_no_merge_different_tag passed\n");
}

static void test_merge_multiple_consecutive(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[20]   = { 0 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;
    SDDL2_tag_registry registry;

    SDDL2_input_buffer_init(&buffer, data, 20);
    SDDL2_segment_list_init(&segments);
    SDDL2_tag_registry_init(&registry);

    // Create 5 consecutive segments with tag=100, sizes: 2, 3, 1, 4, 5
    // Expected: All merge into 1 segment of size 15
    int sizes[] = { 2, 3, 1, 4, 5 };
    for (int i = 0; i < 5; i++) {
        SDDL2_stack_push(stack, SDDL2_value_tag(100));
        SDDL2_stack_push(stack, SDDL2_value_i64(sizes[i]));
        SDDL2_stack_push(
                stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
        SDDL2_error err = SDDL2_op_segment_create_tagged(
                stack, &buffer, &segments, &registry);
        assert(err == SDDL2_OK);
    }

    // Check: 1 merged segment
    assert(segments.count == 1);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 15); // 2+3+1+4+5
    assert(buffer.current_pos == 15);

    // Cleanup
    SDDL2_segment_list_destroy(&segments);
    SDDL2_tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_merge_multiple_consecutive passed\n");
}

static void test_merge_pattern_alternating(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[20]   = { 0 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;
    SDDL2_tag_registry registry;

    SDDL2_input_buffer_init(&buffer, data, 20);
    SDDL2_segment_list_init(&segments);
    SDDL2_tag_registry_init(&registry);

    // Pattern: tag=100 (size=2), tag=200 (size=3), tag=100 (size=1), tag=200
    // (size=2) Expected: 4 segments (no merging due to alternation)
    SDDL2_stack_push(stack, SDDL2_value_tag(100));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_stack_push(stack, SDDL2_value_i64(2));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_stack_push(stack, SDDL2_value_tag(200));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_stack_push(stack, SDDL2_value_i64(3));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_stack_push(stack, SDDL2_value_tag(100));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_stack_push(stack, SDDL2_value_i64(1));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_stack_push(stack, SDDL2_value_tag(200));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_stack_push(stack, SDDL2_value_i64(2));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    // Check: 4 separate segments
    assert(segments.count == 4);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].size_bytes == 2);
    assert(segments.items[1].tag == 200);
    assert(segments.items[1].size_bytes == 3);
    assert(segments.items[2].tag == 100);
    assert(segments.items[2].size_bytes == 1);
    assert(segments.items[3].tag == 200);
    assert(segments.items[3].size_bytes == 2);

    // Cleanup
    SDDL2_segment_list_destroy(&segments);
    SDDL2_tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_merge_pattern_alternating passed\n");
}

static void test_merge_same_tag_after_other_tag(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[20]   = { 0 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;
    SDDL2_tag_registry registry;

    SDDL2_input_buffer_init(&buffer, data, 20);
    SDDL2_segment_list_init(&segments);
    SDDL2_tag_registry_init(&registry);

    // Pattern: tag=100 (size=2), tag=100 (size=3), tag=200 (size=1), tag=200
    // (size=2) Expected: 2 segments: [tag=100, size=5], [tag=200, size=3]
    SDDL2_stack_push(stack, SDDL2_value_tag(100));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_stack_push(stack, SDDL2_value_i64(2));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_stack_push(stack, SDDL2_value_tag(100));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_stack_push(stack, SDDL2_value_i64(3));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_stack_push(stack, SDDL2_value_tag(200));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_stack_push(stack, SDDL2_value_i64(1));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_stack_push(stack, SDDL2_value_tag(200));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_stack_push(stack, SDDL2_value_i64(2));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    // Check: 2 merged segments
    assert(segments.count == 2);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].size_bytes == 5); // Merged!
    assert(segments.items[1].tag == 200);
    assert(segments.items[1].size_bytes == 3); // Merged!

    // Cleanup
    SDDL2_segment_list_destroy(&segments);
    SDDL2_tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_merge_same_tag_after_other_tag passed\n");
}

/* ============================================================================
 * Error Handling Tests
 * ========================================================================= */

static void test_tagged_segment_bounds_error(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;
    SDDL2_tag_registry registry;

    SDDL2_input_buffer_init(&buffer, data, 2);
    SDDL2_segment_list_init(&segments);
    SDDL2_tag_registry_init(&registry);

    // Try to create segment larger than buffer: tag=100, size=10
    SDDL2_stack_push(stack, SDDL2_value_tag(100));
    SDDL2_stack_push(stack, SDDL2_value_i64(10));

    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_SEGMENT_BOUNDS);

    // Nothing should be created
    assert(segments.count == 0);
    assert(buffer.current_pos == 0);

    // Cleanup
    SDDL2_segment_list_destroy(&segments);
    SDDL2_tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_segment_bounds_error passed\n");
}

static void test_tagged_segment_negative_tag(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;
    SDDL2_tag_registry registry;

    SDDL2_input_buffer_init(&buffer, data, 3);
    SDDL2_segment_list_init(&segments);
    SDDL2_tag_registry_init(&registry);

    // Try negative tag: tag=-100, size=2
    SDDL2_stack_push(stack, SDDL2_value_i64(-100));
    SDDL2_stack_push(stack, SDDL2_value_i64(2));

    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Cleanup
    SDDL2_segment_list_destroy(&segments);
    SDDL2_tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_segment_negative_tag passed\n");
}

static void test_tagged_segment_negative_size(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;
    SDDL2_tag_registry registry;

    SDDL2_input_buffer_init(&buffer, data, 3);
    SDDL2_segment_list_init(&segments);
    SDDL2_tag_registry_init(&registry);

    // Try negative size: tag=100, size=-5
    SDDL2_stack_push(stack, SDDL2_value_tag(100));
    SDDL2_stack_push(stack, SDDL2_value_i64(-5));

    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Cleanup
    SDDL2_segment_list_destroy(&segments);
    SDDL2_tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_segment_negative_size passed\n");
}

static void test_tagged_segment_wrong_type_tag(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;
    SDDL2_tag_registry registry;

    SDDL2_input_buffer_init(&buffer, data, 2);
    SDDL2_segment_list_init(&segments);
    SDDL2_tag_registry_init(&registry);

    // Push wrong type for tag (TYPE instead of I64)
    SDDL2_type t = { SDDL2_TYPE_U8, 1 };
    SDDL2_stack_push(stack, SDDL2_value_type(t));
    SDDL2_stack_push(stack, SDDL2_value_i64(2));

    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Cleanup
    SDDL2_segment_list_destroy(&segments);
    SDDL2_tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_segment_wrong_type_tag passed\n");
}

static void test_tagged_segment_wrong_type_size(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;
    SDDL2_tag_registry registry;

    SDDL2_input_buffer_init(&buffer, data, 2);
    SDDL2_segment_list_init(&segments);
    SDDL2_tag_registry_init(&registry);

    // Push correct tag but wrong type for size
    SDDL2_stack_push(stack, SDDL2_value_tag(100));
    SDDL2_stack_push(stack, SDDL2_value_tag(42)); // Wrong type!

    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Cleanup
    SDDL2_segment_list_destroy(&segments);
    SDDL2_tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_segment_wrong_type_size passed\n");
}

static void test_tagged_segment_stack_underflow(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;
    SDDL2_tag_registry registry;

    SDDL2_input_buffer_init(&buffer, data, 2);
    SDDL2_segment_list_init(&segments);
    SDDL2_tag_registry_init(&registry);

    // Push only tag, no size
    SDDL2_stack_push(stack, SDDL2_value_tag(100));

    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_STACK_UNDERFLOW);

    // Cleanup
    SDDL2_segment_list_destroy(&segments);
    SDDL2_tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_segment_stack_underflow passed\n");
}

/* ============================================================================
 * Integration Tests
 * ========================================================================= */

static void test_tagged_with_arithmetic(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[20]   = { 0 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;
    SDDL2_tag_registry registry;

    SDDL2_input_buffer_init(&buffer, data, 20);
    SDDL2_segment_list_init(&segments);
    SDDL2_tag_registry_init(&registry);

    // Compute size with arithmetic: push 2, push 3, add -> 5
    SDDL2_stack_push(stack, SDDL2_value_tag(100)); // tag
    SDDL2_stack_push(stack, SDDL2_value_i64(2));
    SDDL2_stack_push(stack, SDDL2_value_i64(3));
    SDDL2_op_add(stack); // -> 5

    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    assert(segments.count == 1);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].size_bytes == 5); // Computed size!

    // Cleanup
    SDDL2_segment_list_destroy(&segments);
    SDDL2_tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_with_arithmetic passed\n");
}

static void test_mixed_unspecified_and_tagged(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[20]   = { 0 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;
    SDDL2_tag_registry registry;

    SDDL2_input_buffer_init(&buffer, data, 20);
    SDDL2_segment_list_init(&segments);
    SDDL2_tag_registry_init(&registry);

    // Create unspecified segment (tag=0)
    SDDL2_stack_push(stack, SDDL2_value_i64(3));
    SDDL2_op_segment_create_unspecified(stack, &buffer, &segments);

    // Create tagged segment (tag=100)
    SDDL2_stack_push(stack, SDDL2_value_tag(100));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_stack_push(stack, SDDL2_value_i64(2));
    SDDL2_stack_push(stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    // Create another unspecified segment (tag=0)
    SDDL2_stack_push(stack, SDDL2_value_i64(4));
    SDDL2_op_segment_create_unspecified(stack, &buffer, &segments);

    // Check: 3 segments
    assert(segments.count == 3);
    assert(segments.items[0].tag == 0);
    assert(segments.items[0].size_bytes == 3);
    assert(segments.items[1].tag == 100);
    assert(segments.items[1].size_bytes == 2);
    assert(segments.items[2].tag == 0);
    assert(segments.items[2].size_bytes == 4);

    // Cleanup
    SDDL2_segment_list_destroy(&segments);
    SDDL2_tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_mixed_unspecified_and_tagged passed\n");
}

static void test_many_tags_registry_growth(void)
{
    SDDL2_stack* stack = create_test_stack(100);
    uint8_t data[200]  = { 0 };
    SDDL2_input_buffer buffer;
    SDDL2_segment_list segments;
    SDDL2_tag_registry registry;

    SDDL2_input_buffer_init(&buffer, data, 200);
    SDDL2_segment_list_init(&segments);
    SDDL2_tag_registry_init(&registry);

    // Create 50 segments with different tags (should trigger registry growth)
    for (int i = 0; i < 50; i++) {
        SDDL2_stack_push(
                stack, SDDL2_value_tag((uint32_t)(100 + i))); // Different tags
        SDDL2_stack_push(stack, SDDL2_value_i64(2));
        SDDL2_stack_push(
                stack, SDDL2_value_type((SDDL2_type){ SDDL2_TYPE_U8, 1 }));
        SDDL2_error err = SDDL2_op_segment_create_tagged(
                stack, &buffer, &segments, &registry);
        assert(err == SDDL2_OK);
    }

    // Check: 50 segments, all registered
    assert(segments.count == 50);
    assert(registry.count == 50);

    // Verify each segment has correct tag
    for (int i = 0; i < 50; i++) {
        assert(segments.items[i].tag == (uint32_t)(100 + i));
        assert(segments.items[i].size_bytes == 2);
    }

    // Cleanup
    SDDL2_segment_list_destroy(&segments);
    SDDL2_tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_many_tags_registry_growth passed\n");
}

/* ============================================================================
 * Test Runner
 * ========================================================================= */

int main(void)
{
    printf("Running Phase 5 Tagged Segment Tests...\n\n");

    // Tag registry tests
    printf("=== Tag Registry Tests ===\n");
    test_tag_registry_init();
    test_tag_registry_destroy();
    printf("\n");

    // Single segment tests
    printf("=== Single Tagged Segment Tests ===\n");
    test_create_single_tagged_segment();
    test_tagged_segment_zero_size();
    printf("\n");

    // Automatic merging tests (Core Feature!)
    printf("=== Automatic Merging Tests ===\n");
    test_merge_consecutive_same_tag();
    test_no_merge_different_tag();
    test_merge_multiple_consecutive();
    test_merge_pattern_alternating();
    test_merge_same_tag_after_other_tag();
    printf("\n");

    // Error handling tests
    printf("=== Error Handling Tests ===\n");
    test_tagged_segment_bounds_error();
    test_tagged_segment_negative_tag();
    test_tagged_segment_negative_size();
    test_tagged_segment_wrong_type_tag();
    test_tagged_segment_wrong_type_size();
    test_tagged_segment_stack_underflow();
    printf("\n");

    // Integration tests
    printf("=== Integration Tests ===\n");
    test_tagged_with_arithmetic();
    test_mixed_unspecified_and_tagged();
    test_many_tags_registry_growth();
    printf("\n");

    printf("✅ All Phase 5 tests passed! (20 tests)\n");
    return 0;
}
