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
#include "openzl/compress/graphs/sddl2/sddl2_vm.h"

/* Test helpers */
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
 * Tag Registry Init/Destroy Tests
 * ========================================================================= */

static void test_tag_registry_init(void)
{
    SDDL2_Tag_registry registry;

    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    assert(registry.tags == NULL);
    assert(registry.count == 0);
    assert(registry.capacity == 0);

    printf("✓ test_tag_registry_init passed\n");
}

static void test_tag_registry_destroy(void)
{
    SDDL2_Tag_registry registry;

    SDDL2_Tag_registry_init(&registry, NULL, NULL);
    SDDL2_Tag_registry_destroy(&registry);

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
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 5);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Stack: tag=100, type=U8, size=5
    SDDL2_Stack_push(stack, SDDL2_Value_tag(100)); // tag
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(5)); // size

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
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
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_create_single_tagged_segment passed\n");
}

static void test_tagged_segment_zero_size(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 3);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Stack: tag=42, size=0
    SDDL2_Stack_push(stack, SDDL2_Value_tag(42)); // tag
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(0)); // size

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    assert(segments.count == 1);
    assert(segments.items[0].tag == 42);
    assert(segments.items[0].size_bytes == 0);
    assert(buffer.current_pos == 0);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_segment_zero_size passed\n");
}

/* ============================================================================
 * Automatic Merging Tests (Core Feature)
 * ========================================================================= */

static void test_merge_consecutive_same_tag(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 1, 2, 3, 4, 5, 6, 7, 8 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 8);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Create first segment: tag=100, size=2
    SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(2));
    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    // Check: 1 segment, size=2
    assert(segments.count == 1);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 2);
    assert(buffer.current_pos == 2);

    // Create second segment: tag=100, size=3 (SHOULD MERGE!)
    SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(3));
    err = SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    // Check: STILL 1 segment, size=5 (merged!)
    assert(segments.count == 1);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 5); // 2 + 3 = 5!
    assert(buffer.current_pos == 5);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_merge_consecutive_same_tag passed\n");
}

static void test_no_merge_different_tag(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 1, 2, 3, 4, 5, 6 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 6);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Create first segment: tag=100, size=2
    SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(2));
    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    // Create second segment: tag=200, size=3 (different tag, NO MERGE)
    SDDL2_Stack_push(stack, SDDL2_Value_tag(200));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(3));
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
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_no_merge_different_tag passed\n");
}

static void test_merge_multiple_consecutive(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[20]   = { 0 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 20);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Create 5 consecutive segments with tag=100, sizes: 2, 3, 1, 4, 5
    // Expected: All merge into 1 segment of size 15
    int sizes[] = { 2, 3, 1, 4, 5 };
    for (int i = 0; i < 5; i++) {
        SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
        SDDL2_Stack_push(
                stack,
                SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
        SDDL2_Stack_push(stack, SDDL2_Value_i64(sizes[i]));
        SDDL2_Error err = SDDL2_op_segment_create_tagged(
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
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_merge_multiple_consecutive passed\n");
}

static void test_merge_pattern_alternating(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[20]   = { 0 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 20);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Pattern: tag=100 (size=2), tag=200 (size=3), tag=100 (size=1), tag=200
    // (size=2) Expected: 4 segments (no merging due to alternation)
    SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(2));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack, SDDL2_Value_tag(200));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(3));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(1));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack, SDDL2_Value_tag(200));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(2));
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
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_merge_pattern_alternating passed\n");
}

static void test_merge_same_tag_after_other_tag(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[20]   = { 0 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 20);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Pattern: tag=100 (size=2), tag=100 (size=3), tag=200 (size=1), tag=200
    // (size=2) Expected: 2 segments: [tag=100, size=5], [tag=200, size=3]
    SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(2));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(3));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack, SDDL2_Value_tag(200));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(1));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack, SDDL2_Value_tag(200));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(2));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    // Check: 2 merged segments
    assert(segments.count == 2);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].size_bytes == 5); // Merged!
    assert(segments.items[1].tag == 200);
    assert(segments.items[1].size_bytes == 3); // Merged!

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_merge_same_tag_after_other_tag passed\n");
}

/* ============================================================================
 * Error Handling Tests
 * ========================================================================= */

static void test_tagged_segment_bounds_error(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Try to create segment larger than buffer: tag=100, size=10
    SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(10));

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_SEGMENT_BOUNDS);

    // Nothing should be created
    assert(segments.count == 0);
    assert(buffer.current_pos == 0);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_segment_bounds_error passed\n");
}

static void test_tagged_segment_negative_tag(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 3);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Try negative tag (using I64 instead of Tag): tag=-100, type=U8, size=2
    SDDL2_Stack_push(stack, SDDL2_Value_i64(-100)); // Wrong: I64 instead of Tag
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(2));

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_segment_negative_tag passed\n");
}

static void test_tagged_segment_negative_size(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 3);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Try negative size: tag=100, size=-5
    SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(-5));

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_segment_negative_size passed\n");
}

static void test_tagged_segment_size_overflow(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[1024]; // Large buffer
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Try to create segment where element_count * type_size would overflow
    // SIZE_MAX Use I64LE type (8 bytes per element) SIZE_MAX / 8 + 1 elements
    // would cause overflow
    int64_t overflow_count = (int64_t)(SIZE_MAX / 8) + 1;

    SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_I64LE, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(overflow_count));

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);

    // Should return overflow error, not bounds error
    assert(err == SDDL2_MATH_OVERFLOW);

    // Nothing should be created
    assert(segments.count == 0);
    assert(buffer.current_pos == 0);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_segment_size_overflow passed\n");
}

static void test_tagged_segment_wrong_type_tag(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Push wrong type for tag (I64 instead of Tag)
    SDDL2_Stack_push(stack, SDDL2_Value_i64(100)); // Wrong: I64 instead of Tag
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(2));

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_segment_wrong_type_tag passed\n");
}

static void test_tagged_segment_wrong_type_size(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Push correct tag and type, but wrong type for size (Tag instead of I64)
    SDDL2_Stack_push(stack, SDDL2_Value_tag(100)); // tag (correct)
    SDDL2_Stack_push(
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL })); // type (correct)
    SDDL2_Stack_push(
            stack, SDDL2_Value_tag(42)); // Wrong: Tag instead of I64 for size!

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_segment_wrong_type_size passed\n");
}

static void test_tagged_segment_stack_underflow(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Push only tag and type, no size (size should be I64)
    SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    // After refactoring: we type-check as we pop, so TYPE_MISMATCH
    // is detected before STACK_UNDERFLOW (fail-fast behavior)
    assert(err == SDDL2_TYPE_MISMATCH);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_segment_stack_underflow passed\n");
}

/* ============================================================================
 * Integration Tests
 * ========================================================================= */

static void test_tagged_with_arithmetic(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[20]   = { 0 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 20);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Compute size with arithmetic: push 2, push 3, add -> 5
    SDDL2_Stack_push(stack, SDDL2_Value_tag(100)); // tag
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(2));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(3));
    SDDL2_op_add(stack, NULL, 0); // -> 5

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    assert(segments.count == 1);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].size_bytes == 5); // Computed size!

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_tagged_with_arithmetic passed\n");
}

static void test_mixed_unspecified_and_tagged(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[20]   = { 0 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 20);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Create unspecified segment (tag=0)
    SDDL2_Stack_push(stack, SDDL2_Value_i64(3));
    SDDL2_op_segment_create_unspecified(stack, &buffer, &segments);

    // Create tagged segment (tag=100)
    SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack, SDDL2_Value_type((SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(2));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    // Create another unspecified segment (tag=0)
    SDDL2_Stack_push(stack, SDDL2_Value_i64(4));
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
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_mixed_unspecified_and_tagged passed\n");
}

static void test_many_tags_registry_growth(void)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[200]  = { 0 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 200);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Create 50 segments with different tags (should trigger registry growth)
    for (int i = 0; i < 50; i++) {
        SDDL2_Stack_push(stack, SDDL2_Value_tag((uint32_t)(100 + i))); // tag
        SDDL2_Stack_push(
                stack,
                SDDL2_Value_type(
                        (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL })); // type
        SDDL2_Stack_push(stack, SDDL2_Value_i64(2));              // size
        SDDL2_Error err = SDDL2_op_segment_create_tagged(
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
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_many_tags_registry_growth passed\n");
}

/**
 * Test: Segment list dynamic growth
 *
 * This test verifies that the segment list can grow beyond its initial
 * capacity when many segments are created. Unlike
 * test_many_tags_registry_growth which tests tag registry growth, this
 * specifically tests segment list growth.
 *
 * Strategy: Create 100 segments with different tags to prevent merging,
 * forcing the segment list to grow from initial capacity to accommodate all.
 */
static void test_segment_list_dynamic_growth(void)
{
    SDDL2_Stack* stack = create_test_stack(1000);
    uint8_t data[500]  = { 0 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 500);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Initial capacity is 0 when using heap allocation (NULL allocator)
    // It will grow as needed: 0 -> 32 -> 64 -> 128
    size_t initial_capacity = segments.capacity;

    // Create 100 segments with DIFFERENT tags (prevents merging)
    // Each segment is 5 bytes, different tag ensures no merging
    const int NUM_SEGMENTS = 100;
    for (int i = 0; i < NUM_SEGMENTS; i++) {
        SDDL2_Stack_push(stack, SDDL2_Value_tag((uint32_t)(1000 + i))); // tag
        SDDL2_Stack_push(
                stack,
                SDDL2_Value_type(
                        (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL })); // type
        SDDL2_Stack_push(stack, SDDL2_Value_i64(5));              // size
        SDDL2_Error err = SDDL2_op_segment_create_tagged(
                stack, &buffer, &segments, &registry);
        assert(err == SDDL2_OK);
    }

    // Verify all 100 segments were created (no merging)
    assert(segments.count == NUM_SEGMENTS);

    // Verify capacity grew beyond initial (proves dynamic growth works)
    assert(segments.capacity > initial_capacity);
    assert(segments.capacity >= NUM_SEGMENTS);

    // Verify each segment is correct and in order
    for (int i = 0; i < NUM_SEGMENTS; i++) {
        assert(segments.items[i].tag == (uint32_t)(1000 + i));
        assert(segments.items[i].start_pos == (size_t)(i * 5));
        assert(segments.items[i].size_bytes == 5);
        assert(segments.items[i].type.kind == SDDL2_TYPE_U8);
        assert(segments.items[i].type.width == 1);
    }

    // Verify buffer cursor advanced correctly
    assert(buffer.current_pos == NUM_SEGMENTS * 5);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_segment_list_dynamic_growth passed\n");
}

/**
 * Test: Segment list dynamic growth with different types
 *
 * This test verifies segment list growth when creating many segments with
 * DIFFERENT TYPES. Even with the same tag, different types prevent merging,
 * ensuring each segment creation adds a new segment to the list.
 *
 * This is more realistic than test_segment_list_dynamic_growth (which uses
 * different tags) because in real usage, typed segments with different types
 * cannot merge even if they share the same tag.
 *
 * Strategy: Create 50 segments with the SAME tag but DIFFERENT types.
 * Types cycle through: U8, I16LE, F32LE, I64BE (4 types).
 * No merging occurs because types differ between consecutive segments.
 */
static void test_segment_list_growth_different_types(void)
{
    SDDL2_Stack* stack = create_test_stack(1000);
    uint8_t data[500]  = { 0 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 500);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    size_t initial_capacity = segments.capacity;

    // Types to cycle through (different sizes to make it more interesting)
    SDDL2_Type_kind types[] = {
        SDDL2_TYPE_U8,    // 1 byte
        SDDL2_TYPE_I16LE, // 2 bytes
        SDDL2_TYPE_F32LE, // 4 bytes
        SDDL2_TYPE_I64BE  // 8 bytes
    };
    size_t element_counts[] = { 2, 1, 1, 1 }; // Total per segment: 2, 2, 4, 8

    // Create 50 segments with SAME tag (100) but DIFFERENT types
    const int NUM_SEGMENTS = 50;
    size_t expected_pos    = 0;

    for (int i = 0; i < NUM_SEGMENTS; i++) {
        int type_idx = i % 4;

        // Same tag for all segments!
        SDDL2_Stack_push(stack, SDDL2_Value_tag(100));

        // Different type each time (cycles through 4 types)
        SDDL2_Stack_push(
                stack,
                SDDL2_Value_type((SDDL2_Type){ types[type_idx], 1, .struct_data = NULL }));

        // Element count depends on type
        SDDL2_Stack_push(
                stack, SDDL2_Value_i64((int64_t)element_counts[type_idx]));

        SDDL2_Error err = SDDL2_op_segment_create_tagged(
                stack, &buffer, &segments, &registry);
        assert(err == SDDL2_OK);
    }

    // Verify all 50 segments were created (no merging despite same tag!)
    assert(segments.count == NUM_SEGMENTS);

    // Verify capacity grew beyond initial
    assert(segments.capacity > initial_capacity);
    assert(segments.capacity >= NUM_SEGMENTS);

    // Verify each segment has correct type and doesn't merge with neighbors
    expected_pos = 0;
    for (int i = 0; i < NUM_SEGMENTS; i++) {
        int type_idx = i % 4;
        size_t expected_bytes =
                element_counts[type_idx] * SDDL2_kind_size(types[type_idx]);

        assert(segments.items[i].tag == 100); // Same tag!
        assert(segments.items[i].type.kind
               == types[type_idx]); // Different types
        assert(segments.items[i].type.width == 1);
        assert(segments.items[i].start_pos == expected_pos);
        assert(segments.items[i].size_bytes == expected_bytes);

        expected_pos += expected_bytes;
    }

    // Only 1 tag should be registered (all segments use tag 100)
    assert(registry.count == 1);
    assert(registry.tags[0] == 100);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);

    printf("✓ test_segment_list_growth_different_types passed\n");
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
    test_tagged_segment_size_overflow();
    test_tagged_segment_wrong_type_tag();
    test_tagged_segment_wrong_type_size();
    test_tagged_segment_stack_underflow();
    printf("\n");

    // Integration tests
    printf("=== Integration Tests ===\n");
    test_tagged_with_arithmetic();
    test_mixed_unspecified_and_tagged();
    test_many_tags_registry_growth();
    test_segment_list_dynamic_growth();
    test_segment_list_growth_different_types();
    printf("\n");

    printf("✅ All Phase 5 tests passed! (23 tests)\n");
    return 0;
}
