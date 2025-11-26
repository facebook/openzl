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
#include "sddl2_test_framework.h"

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

TEST(test_tag_registry_init)
{
    SDDL2_Tag_registry registry;

    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    assert(registry.entries == NULL);
    assert(registry.count == 0);
    assert(registry.capacity == 0);
}

TEST(test_tag_registry_destroy)
{
    SDDL2_Tag_registry registry;

    SDDL2_Tag_registry_init(&registry, NULL, NULL);
    SDDL2_Tag_registry_destroy(&registry);

    assert(registry.entries == NULL);
    assert(registry.count == 0);
    assert(registry.capacity == 0);
}

/* ============================================================================
 * Single Tagged Segment Tests
 * ========================================================================= */

TEST(test_create_single_tagged_segment)
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
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
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
    assert(registry.entries[0].tag == 100);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);
}

TEST(test_tagged_segment_zero_size)
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
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
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
}

/* ============================================================================
 * Automatic Merging Tests (Core Feature)
 * ========================================================================= */

TEST(test_merge_consecutive_same_tag)
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
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
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
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
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
}

TEST(test_no_merge_different_tag)
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
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(2));
    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    // Create second segment: tag=200, size=3 (different tag, NO MERGE)
    SDDL2_Stack_push(stack, SDDL2_Value_tag(200));
    SDDL2_Stack_push(
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
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
}

TEST(test_merge_multiple_consecutive)
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
                SDDL2_Value_type(
                        (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
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
}

TEST(test_merge_pattern_alternating)
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
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(2));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack, SDDL2_Value_tag(200));
    SDDL2_Stack_push(
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(3));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(1));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack, SDDL2_Value_tag(200));
    SDDL2_Stack_push(
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
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
}

TEST(test_merge_same_tag_after_other_tag)
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
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(2));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(3));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack, SDDL2_Value_tag(200));
    SDDL2_Stack_push(
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(1));
    SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack, SDDL2_Value_tag(200));
    SDDL2_Stack_push(
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
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
}

TEST(test_no_merge_different_struct_types_same_size)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[32]   = { 0 }; // 16 + 16 bytes
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 32);
    SDDL2_Segment_list_init(&segments, NULL, NULL);
    SDDL2_Tag_registry_init(&registry, NULL, NULL);

    // Create first structure type: {U8, U8, I16LE, I32LE, I64LE}
    // Size: 1 + 1 + 2 + 4 + 8 = 16 bytes
    SDDL2_Struct_data* struct1_data =
            malloc(sizeof(SDDL2_Struct_data) + 5 * sizeof(SDDL2_Type));
    assert(struct1_data != NULL);

    struct1_data->member_count = 5;
    struct1_data->members[0] =
            (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL };
    struct1_data->members[1] =
            (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL };
    struct1_data->members[2] =
            (SDDL2_Type){ SDDL2_TYPE_I16LE, 1, .struct_data = NULL };
    struct1_data->members[3] =
            (SDDL2_Type){ SDDL2_TYPE_I32LE, 1, .struct_data = NULL };
    struct1_data->members[4] =
            (SDDL2_Type){ SDDL2_TYPE_I64LE, 1, .struct_data = NULL };
    struct1_data->total_size_bytes = 16;

    SDDL2_Type type1 = { .kind        = SDDL2_TYPE_STRUCTURE,
                         .width       = 1, // Single structure instance
                         .struct_data = struct1_data };

    // Create second structure type: {I64LE, I32LE, I16LE, U8, U8}
    // Size: 8 + 4 + 2 + 1 + 1 = 16 bytes (SAME size, DIFFERENT layout!)
    SDDL2_Struct_data* struct2_data =
            malloc(sizeof(SDDL2_Struct_data) + 5 * sizeof(SDDL2_Type));
    assert(struct2_data != NULL);

    struct2_data->member_count = 5;
    struct2_data->members[0] =
            (SDDL2_Type){ SDDL2_TYPE_I64LE, 1, .struct_data = NULL };
    struct2_data->members[1] =
            (SDDL2_Type){ SDDL2_TYPE_I32LE, 1, .struct_data = NULL };
    struct2_data->members[2] =
            (SDDL2_Type){ SDDL2_TYPE_I16LE, 1, .struct_data = NULL };
    struct2_data->members[3] =
            (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL };
    struct2_data->members[4] =
            (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL };
    struct2_data->total_size_bytes = 16;

    SDDL2_Type type2 = { .kind        = SDDL2_TYPE_STRUCTURE,
                         .width       = 1, // Single structure instance
                         .struct_data = struct2_data };

    // Create first segment: tag=100, type=struct1, size=16 bytes
    SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
    SDDL2_Stack_push(stack, SDDL2_Value_type(type1));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(1)); // 1 element
    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_OK);

    // Check: 1 segment, size=16
    assert(segments.count == 1);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 16);
    assert(buffer.current_pos == 16);

    // Create second segment: tag=100 (SAME tag!), type=struct2, size=16 bytes
    // This should FAIL because tag 100 is already registered with a different
    // type! The semantic constraint is: a tag uniquely identifies a type.
    SDDL2_Stack_push(stack, SDDL2_Value_tag(100));
    SDDL2_Stack_push(stack, SDDL2_Value_type(type2));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(1)); // 1 element
    err = SDDL2_op_segment_create_tagged(stack, &buffer, &segments, &registry);

    // Should fail with TYPE_MISMATCH because tag 100 already has a different
    // type!
    assert(err == SDDL2_TYPE_MISMATCH);

    // Only the first segment should exist
    assert(segments.count == 1);
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 16);
    assert(buffer.current_pos == 16); // Cursor not advanced for failed segment

    // Only 1 tag should be registered (first registration with struct1)
    assert(registry.count == 1);
    assert(registry.entries[0].tag == 100);

    // Cleanup
    free(struct1_data);
    free(struct2_data);
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);
}

/* ============================================================================
 * Error Handling Tests
 * ========================================================================= */

TEST(test_tagged_segment_bounds_error)
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
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
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
}

TEST(test_tagged_segment_negative_tag)
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
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(2));

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);
}

TEST(test_tagged_segment_negative_size)
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
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(-5));

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);
}

TEST(test_tagged_segment_size_overflow)
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
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_I64LE, 1, .struct_data = NULL }));
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
}

TEST(test_tagged_segment_wrong_type_tag)
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
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
    SDDL2_Stack_push(stack, SDDL2_Value_i64(2));

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);
}

TEST(test_tagged_segment_wrong_type_size)
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
            SDDL2_Value_type((SDDL2_Type){
                    SDDL2_TYPE_U8, 1, .struct_data = NULL })); // type (correct)
    SDDL2_Stack_push(
            stack, SDDL2_Value_tag(42)); // Wrong: Tag instead of I64 for size!

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    assert(err == SDDL2_TYPE_MISMATCH);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);
}

TEST(test_tagged_segment_stack_underflow)
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
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));

    SDDL2_Error err = SDDL2_op_segment_create_tagged(
            stack, &buffer, &segments, &registry);
    // After refactoring: we type-check as we pop, so TYPE_MISMATCH
    // is detected before STACK_UNDERFLOW (fail-fast behavior)
    assert(err == SDDL2_TYPE_MISMATCH);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);
}

/* ============================================================================
 * Integration Tests
 * ========================================================================= */

TEST(test_tagged_with_arithmetic)
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
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
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
}

TEST(test_mixed_unspecified_and_tagged)
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
            stack,
            SDDL2_Value_type(
                    (SDDL2_Type){ SDDL2_TYPE_U8, 1, .struct_data = NULL }));
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
}

TEST(test_many_tags_registry_growth)
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
                SDDL2_Value_type((SDDL2_Type){
                        SDDL2_TYPE_U8, 1, .struct_data = NULL })); // type
        SDDL2_Stack_push(stack, SDDL2_Value_i64(2));               // size
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
TEST(test_segment_list_dynamic_growth)
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
                SDDL2_Value_type((SDDL2_Type){
                        SDDL2_TYPE_U8, 1, .struct_data = NULL })); // type
        SDDL2_Stack_push(stack, SDDL2_Value_i64(5));               // size
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
}

/**
 * Test: Segment list dynamic growth with different types
 *
 * This test verifies segment list growth when creating many segments with
 * DIFFERENT TYPES. Each type uses a different tag (as required by the semantic
 * constraint that a tag uniquely identifies a type).
 *
 * Strategy: Create 50 segments with DIFFERENT tags and DIFFERENT types.
 * Types cycle through: U8, I16LE, F32LE, I64BE (4 types).
 * Tags are assigned uniquely per segment to comply with tag-type uniqueness.
 * No merging occurs because tags differ between consecutive segments.
 */
TEST(test_segment_list_growth_different_types)
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

    // Create 50 segments with DIFFERENT tags and DIFFERENT types
    const int NUM_SEGMENTS = 50;
    size_t expected_pos    = 0;

    for (int i = 0; i < NUM_SEGMENTS; i++) {
        int type_idx = i % 4;

        // Different tag for each segment (to comply with tag-type uniqueness)
        SDDL2_Stack_push(stack, SDDL2_Value_tag((uint32_t)(100 + i)));

        // Different type each time (cycles through 4 types)
        SDDL2_Stack_push(
                stack,
                SDDL2_Value_type((SDDL2_Type){
                        types[type_idx], 1, .struct_data = NULL }));

        // Element count depends on type
        SDDL2_Stack_push(
                stack, SDDL2_Value_i64((int64_t)element_counts[type_idx]));

        SDDL2_Error err = SDDL2_op_segment_create_tagged(
                stack, &buffer, &segments, &registry);
        assert(err == SDDL2_OK);
    }

    // Verify all 50 segments were created (no merging - different tags!)
    assert(segments.count == NUM_SEGMENTS);

    // Verify capacity grew beyond initial
    assert(segments.capacity > initial_capacity);
    assert(segments.capacity >= NUM_SEGMENTS);

    // Verify each segment has correct type and tag
    expected_pos = 0;
    for (int i = 0; i < NUM_SEGMENTS; i++) {
        int type_idx = i % 4;
        size_t expected_bytes =
                element_counts[type_idx] * SDDL2_kind_size(types[type_idx]);

        assert(segments.items[i].tag == (uint32_t)(100 + i)); // Different tags
        assert(segments.items[i].type.kind
               == types[type_idx]); // Different types
        assert(segments.items[i].type.width == 1);
        assert(segments.items[i].start_pos == expected_pos);
        assert(segments.items[i].size_bytes == expected_bytes);

        expected_pos += expected_bytes;
    }

    // 50 tags should be registered (each segment uses unique tag)
    assert(registry.count == NUM_SEGMENTS);

    // Cleanup
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
    destroy_test_stack(stack);
}

/* ============================================================================
 * Test Runner
 * ========================================================================= */

int main(void)
{
    return sddl2_run_all_tests();
}
