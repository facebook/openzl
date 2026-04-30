// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * Unit tests for OpenZL VM tagged segments with automatic merging.
 * Tests Phase 5: Tag Registry & Segment Merging
 *
 * This tests tagged segments and the automatic merging behavior:
 * - When consecutive segments have the same tag, they merge automatically
 * - When tags differ or positions are not consecutive, new segments are created
 */

#include <climits>

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2TaggedSegmentsTest : public SDDL2StackTest {};
class SDDL2TaggedSegmentsLargeTest : public SDDL2StackTestCustomCapacity<1000> {
};
} // namespace

// ============================================================================
// Tag Registry Init/Destroy Tests
// ============================================================================

TEST_F(SDDL2TaggedSegmentsTest, TagRegistryInit)
{
    SDDL2_Tag_registry registry;
    SDDL2_Tag_registry_init(&registry, nullptr, nullptr);

    EXPECT_EQ(registry.entries, nullptr);
    EXPECT_EQ(registry.count, 0u);
    EXPECT_EQ(registry.capacity, 0u);
}

TEST_F(SDDL2TaggedSegmentsTest, TagRegistryDestroy)
{
    SDDL2_Tag_registry registry;
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_destroy(&registry);

    EXPECT_EQ(registry.entries, nullptr);
    EXPECT_EQ(registry.count, 0u);
    EXPECT_EQ(registry.capacity, 0u);
}

// ============================================================================
// Single Tagged Segment Tests
// ============================================================================

TEST_F(SDDL2TaggedSegmentsTest, CreateSingleTaggedSegment)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 5);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Stack: tag=100, type=U8, size=5
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(5));

    ASSERT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_OK);

    // Check segment
    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(segments.items[0].tag, 100u);
    EXPECT_EQ(segments.items[0].start_pos, 0u);
    EXPECT_EQ(segments.items[0].size_bytes, 5u);

    // Check buffer advanced
    EXPECT_EQ(buffer.current_pos, 5u);

    // Check tag registered
    EXPECT_EQ(registry.count, 1u);
    EXPECT_EQ(registry.entries[0].tag, 100u);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2TaggedSegmentsTest, TaggedSegmentZeroSize)
{
    uint8_t data[] = { 0x01, 0x02, 0x03 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 3);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Stack: tag=42, size=0
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(42));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(0));

    ASSERT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_OK);

    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(segments.items[0].tag, 42u);
    EXPECT_EQ(segments.items[0].size_bytes, 0u);
    EXPECT_EQ(buffer.current_pos, 0u);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

// ============================================================================
// Automatic Merging Tests (Core Feature)
// ============================================================================

TEST_F(SDDL2TaggedSegmentsTest, MergeConsecutiveSameTag)
{
    uint8_t data[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 8);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Create first segment: tag=100, size=2
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(2));
    ASSERT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_OK);

    // Check: 1 segment, size=2
    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(segments.items[0].tag, 100u);
    EXPECT_EQ(segments.items[0].start_pos, 0u);
    EXPECT_EQ(segments.items[0].size_bytes, 2u);
    EXPECT_EQ(buffer.current_pos, 2u);

    // Create second segment: tag=100, size=3 (SHOULD MERGE!)
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(3));
    ASSERT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_OK);

    // Check: STILL 1 segment, size=5 (merged!)
    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(segments.items[0].tag, 100u);
    EXPECT_EQ(segments.items[0].start_pos, 0u);
    EXPECT_EQ(segments.items[0].size_bytes, 5u); // 2 + 3 = 5!
    EXPECT_EQ(buffer.current_pos, 5u);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2TaggedSegmentsTest, NoMergeDifferentTag)
{
    uint8_t data[] = { 1, 2, 3, 4, 5, 6 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 6);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Create first segment: tag=100, size=2
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(2));
    ASSERT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_OK);

    // Create second segment: tag=200, size=3 (different tag, NO MERGE)
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(200));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(3));
    ASSERT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_OK);

    // Check: 2 separate segments
    EXPECT_EQ(segments.count, 2u);
    EXPECT_EQ(segments.items[0].tag, 100u);
    EXPECT_EQ(segments.items[0].start_pos, 0u);
    EXPECT_EQ(segments.items[0].size_bytes, 2u);
    EXPECT_EQ(segments.items[1].tag, 200u);
    EXPECT_EQ(segments.items[1].start_pos, 2u);
    EXPECT_EQ(segments.items[1].size_bytes, 3u);
    EXPECT_EQ(buffer.current_pos, 5u);

    // Check both tags registered
    EXPECT_EQ(registry.count, 2u);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2TaggedSegmentsTest, MergeMultipleConsecutive)
{
    uint8_t data[20] = { 0 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 20);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Create 5 consecutive segments with tag=100, sizes: 2, 3, 1, 4, 5
    // Expected: All merge into 1 segment of size 15
    int sizes[] = { 2, 3, 1, 4, 5 };
    for (int i = 0; i < 5; i++) {
        SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
        SDDL2_Stack_push(
                stack_,
                SDDL2_Value_type(
                        SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                    .width       = 1,
                                    .struct_data = nullptr }));
        SDDL2_Stack_push(stack_, SDDL2_Value_i64(sizes[i]));
        ASSERT_EQ(
                SDDL2_op_segment_create_tagged(
                        stack_, &buffer, &segments, &registry),
                SDDL2_OK);
    }

    // Check: 1 merged segment
    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(segments.items[0].tag, 100u);
    EXPECT_EQ(segments.items[0].start_pos, 0u);
    EXPECT_EQ(segments.items[0].size_bytes, 15u); // 2+3+1+4+5
    EXPECT_EQ(buffer.current_pos, 15u);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2TaggedSegmentsTest, MergePatternAlternating)
{
    uint8_t data[20] = { 0 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 20);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Pattern: tag=100 (size=2), tag=200 (size=3), tag=100 (size=1), tag=200
    // (size=2) Expected: 4 segments (no merging due to alternation)
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(2));
    SDDL2_op_segment_create_tagged(stack_, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack_, SDDL2_Value_tag(200));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(3));
    SDDL2_op_segment_create_tagged(stack_, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(1));
    SDDL2_op_segment_create_tagged(stack_, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack_, SDDL2_Value_tag(200));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(2));
    SDDL2_op_segment_create_tagged(stack_, &buffer, &segments, &registry);

    // Check: 4 separate segments
    EXPECT_EQ(segments.count, 4u);
    EXPECT_EQ(segments.items[0].tag, 100u);
    EXPECT_EQ(segments.items[0].size_bytes, 2u);
    EXPECT_EQ(segments.items[1].tag, 200u);
    EXPECT_EQ(segments.items[1].size_bytes, 3u);
    EXPECT_EQ(segments.items[2].tag, 100u);
    EXPECT_EQ(segments.items[2].size_bytes, 1u);
    EXPECT_EQ(segments.items[3].tag, 200u);
    EXPECT_EQ(segments.items[3].size_bytes, 2u);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2TaggedSegmentsTest, MergeSameTagAfterOtherTag)
{
    uint8_t data[20] = { 0 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 20);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Pattern: tag=100 (size=2), tag=100 (size=3), tag=200 (size=1), tag=200
    // (size=2) Expected: 2 segments: [tag=100, size=5], [tag=200, size=3]
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(2));
    SDDL2_op_segment_create_tagged(stack_, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(3));
    SDDL2_op_segment_create_tagged(stack_, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack_, SDDL2_Value_tag(200));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(1));
    SDDL2_op_segment_create_tagged(stack_, &buffer, &segments, &registry);

    SDDL2_Stack_push(stack_, SDDL2_Value_tag(200));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(2));
    SDDL2_op_segment_create_tagged(stack_, &buffer, &segments, &registry);

    // Check: 2 merged segments
    EXPECT_EQ(segments.count, 2u);
    EXPECT_EQ(segments.items[0].tag, 100u);
    EXPECT_EQ(segments.items[0].size_bytes, 5u); // Merged!
    EXPECT_EQ(segments.items[1].tag, 200u);
    EXPECT_EQ(segments.items[1].size_bytes, 3u); // Merged!

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2TaggedSegmentsTest, NoMergeDifferentStructTypesSameSize)
{
    uint8_t data[32] = { 0 }; // 16 + 16 bytes
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 32);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Create first structure type: {U8, U8, I16LE, I32LE, I64LE}
    // Size: 1 + 1 + 2 + 4 + 8 = 16 bytes
    SDDL2_Struct_data* struct1_data = (SDDL2_Struct_data*)malloc(
            sizeof(SDDL2_Struct_data) + 5 * sizeof(SDDL2_Type));
    ASSERT_NE(struct1_data, nullptr);

    struct1_data->member_count     = 5;
    struct1_data->members[0]       = SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                                 .width       = 1,
                                                 .struct_data = nullptr };
    struct1_data->members[1]       = SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                                 .width       = 1,
                                                 .struct_data = nullptr };
    struct1_data->members[2]       = SDDL2_Type{ .kind        = SDDL2_TYPE_I16LE,
                                                 .width       = 1,
                                                 .struct_data = nullptr };
    struct1_data->members[3]       = SDDL2_Type{ .kind        = SDDL2_TYPE_I32LE,
                                                 .width       = 1,
                                                 .struct_data = nullptr };
    struct1_data->members[4]       = SDDL2_Type{ .kind        = SDDL2_TYPE_I64LE,
                                                 .width       = 1,
                                                 .struct_data = nullptr };
    struct1_data->total_size_bytes = 16;

    SDDL2_Type type1 = { .kind        = SDDL2_TYPE_STRUCTURE,
                         .width       = 1,
                         .struct_data = struct1_data };

    // Create second structure type: {I64LE, I32LE, I16LE, U8, U8}
    // Size: 8 + 4 + 2 + 1 + 1 = 16 bytes (SAME size, DIFFERENT layout!)
    SDDL2_Struct_data* struct2_data = (SDDL2_Struct_data*)malloc(
            sizeof(SDDL2_Struct_data) + 5 * sizeof(SDDL2_Type));
    ASSERT_NE(struct2_data, nullptr);

    struct2_data->member_count     = 5;
    struct2_data->members[0]       = SDDL2_Type{ .kind        = SDDL2_TYPE_I64LE,
                                                 .width       = 1,
                                                 .struct_data = nullptr };
    struct2_data->members[1]       = SDDL2_Type{ .kind        = SDDL2_TYPE_I32LE,
                                                 .width       = 1,
                                                 .struct_data = nullptr };
    struct2_data->members[2]       = SDDL2_Type{ .kind        = SDDL2_TYPE_I16LE,
                                                 .width       = 1,
                                                 .struct_data = nullptr };
    struct2_data->members[3]       = SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                                 .width       = 1,
                                                 .struct_data = nullptr };
    struct2_data->members[4]       = SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                                 .width       = 1,
                                                 .struct_data = nullptr };
    struct2_data->total_size_bytes = 16;

    SDDL2_Type type2 = { .kind        = SDDL2_TYPE_STRUCTURE,
                         .width       = 1,
                         .struct_data = struct2_data };

    // Create first segment: tag=100, type=struct1, size=16 bytes
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(stack_, SDDL2_Value_type(type1));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)); // 1 element
    ASSERT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_OK);

    // Check: 1 segment, size=16
    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(segments.items[0].tag, 100u);
    EXPECT_EQ(segments.items[0].start_pos, 0u);
    EXPECT_EQ(segments.items[0].size_bytes, 16u);
    EXPECT_EQ(buffer.current_pos, 16u);

    // Create second segment: tag=100 (SAME tag!), type=struct2, size=16 bytes
    // This should FAIL because tag 100 is already registered with a different
    // type!
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(stack_, SDDL2_Value_type(type2));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)); // 1 element
    // Should fail with TYPE_MISMATCH because tag 100 already has a different
    // type!
    EXPECT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_TYPE_MISMATCH);

    // Only the first segment should exist
    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(segments.items[0].tag, 100u);
    EXPECT_EQ(segments.items[0].start_pos, 0u);
    EXPECT_EQ(segments.items[0].size_bytes, 16u);
    EXPECT_EQ(buffer.current_pos, 16u);

    // Only 1 tag should be registered
    EXPECT_EQ(registry.count, 1u);
    EXPECT_EQ(registry.entries[0].tag, 100u);

    free(struct1_data);
    free(struct2_data);
    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(SDDL2TaggedSegmentsTest, TaggedSegmentBoundsError)
{
    uint8_t data[] = { 0x01, 0x02 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Try to create segment larger than buffer: tag=100, size=10
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(10));

    EXPECT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_SEGMENT_BOUNDS);

    // Nothing should be created
    EXPECT_EQ(segments.count, 0u);
    EXPECT_EQ(buffer.current_pos, 0u);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2TaggedSegmentsTest, TaggedSegmentNegativeTag)
{
    uint8_t data[] = { 0x01, 0x02, 0x03 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 3);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Try negative tag (using I64 instead of Tag): tag=-100, type=U8, size=2
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(-100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(2));

    EXPECT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_TYPE_MISMATCH);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2TaggedSegmentsTest, TaggedSegmentNegativeSize)
{
    uint8_t data[] = { 0x01, 0x02, 0x03 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 3);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Try negative size: tag=100, size=-5
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(-5));

    EXPECT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_TYPE_MISMATCH);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2TaggedSegmentsTest, TaggedSegmentSizeOverflow)
{
    uint8_t data[1024] = { 0 }; // Large buffer
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Try to create segment where element_count * type_size would overflow
    int64_t overflow_count = (int64_t)(SIZE_MAX / 8) + 1;

    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_I64LE,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(overflow_count));

    // Should return overflow error, not bounds error
    EXPECT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_MATH_OVERFLOW);

    // Nothing should be created
    EXPECT_EQ(segments.count, 0u);
    EXPECT_EQ(buffer.current_pos, 0u);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2TaggedSegmentsTest, TaggedSegmentWrongTypeTag)
{
    uint8_t data[] = { 0x01, 0x02 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Push wrong type for tag (I64 instead of Tag)
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(2));

    EXPECT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_TYPE_MISMATCH);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2TaggedSegmentsTest, TaggedSegmentWrongTypeSize)
{
    uint8_t data[] = { 0x01, 0x02 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Push correct tag and type, but wrong type for size (Tag instead of I64)
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(42));

    EXPECT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_TYPE_MISMATCH);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2TaggedSegmentsTest, TaggedSegmentStackUnderflow)
{
    uint8_t data[] = { 0x01, 0x02 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Push only tag and type, no size
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));

    // After refactoring: we type-check as we pop, so TYPE_MISMATCH
    // is detected before STACK_UNDERFLOW (fail-fast behavior)
    EXPECT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_TYPE_MISMATCH);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(SDDL2TaggedSegmentsTest, TaggedWithArithmetic)
{
    uint8_t data[20] = { 0 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 20);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Compute size with arithmetic: push 2, push 3, add -> 5
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(2));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(3));
    SDDL2_op_add(stack_, NULL, 0); // -> 5

    ASSERT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_OK);

    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(segments.items[0].tag, 100u);
    EXPECT_EQ(segments.items[0].size_bytes, 5u); // Computed size!

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2TaggedSegmentsTest, MixedUnspecifiedAndTagged)
{
    uint8_t data[20] = { 0 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 20);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Create unspecified segment (tag=0)
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(3));
    SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments);

    // Create tagged segment (tag=100)
    SDDL2_Stack_push(stack_, SDDL2_Value_tag(100));
    SDDL2_Stack_push(
            stack_,
            SDDL2_Value_type(
                    SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                .width       = 1,
                                .struct_data = nullptr }));
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(2));
    SDDL2_op_segment_create_tagged(stack_, &buffer, &segments, &registry);

    // Create another unspecified segment (tag=0)
    SDDL2_Stack_push(stack_, SDDL2_Value_i64(4));
    SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments);

    // Check: 3 segments
    EXPECT_EQ(segments.count, 3u);
    EXPECT_EQ(segments.items[0].tag, 0u);
    EXPECT_EQ(segments.items[0].size_bytes, 3u);
    EXPECT_EQ(segments.items[1].tag, 100u);
    EXPECT_EQ(segments.items[1].size_bytes, 2u);
    EXPECT_EQ(segments.items[2].tag, 0u);
    EXPECT_EQ(segments.items[2].size_bytes, 4u);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2TaggedSegmentsTest, ManyTagsRegistryGrowth)
{
    uint8_t data[200] = { 0 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data, 200);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Create 50 segments with different tags (should trigger registry growth)
    for (int i = 0; i < 50; i++) {
        SDDL2_Stack_push(stack_, SDDL2_Value_tag((uint32_t)(100 + i)));
        SDDL2_Stack_push(
                stack_,
                SDDL2_Value_type(
                        SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                    .width       = 1,
                                    .struct_data = nullptr }));
        SDDL2_Stack_push(stack_, SDDL2_Value_i64(2));
        ASSERT_EQ(
                SDDL2_op_segment_create_tagged(
                        stack_, &buffer, &segments, &registry),
                SDDL2_OK);
    }

    // Check: 50 segments, all registered
    EXPECT_EQ(segments.count, 50u);
    EXPECT_EQ(registry.count, 50u);

    // Verify each segment has correct tag
    for (int i = 0; i < 50; i++) {
        EXPECT_EQ(segments.items[i].tag, (uint32_t)(100 + i));
        EXPECT_EQ(segments.items[i].size_bytes, 2u);
    }

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

// ============================================================================
// Dynamic Growth Tests
// ============================================================================

TEST_F(SDDL2TaggedSegmentsLargeTest, SegmentListDynamicGrowth)
{
    const int NUM_SEGMENTS = SDDL2_SEGMENT_INITIAL_CAPACITY + 1;
    const int SEGMENT_SIZE = 5;

    std::vector<uint8_t> data(NUM_SEGMENTS * SEGMENT_SIZE);
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data.data(), data.size());
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    size_t initial_capacity = segments.capacity;

    // Create NUM_SEGMENTS segments with DIFFERENT tags (prevents merging)
    for (int i = 0; i < NUM_SEGMENTS; i++) {
        SDDL2_Stack_push(stack_, SDDL2_Value_tag((uint32_t)(1000 + i)));
        SDDL2_Stack_push(
                stack_,
                SDDL2_Value_type(
                        SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                    .width       = 1,
                                    .struct_data = nullptr }));
        SDDL2_Stack_push(stack_, SDDL2_Value_i64(SEGMENT_SIZE));
        ASSERT_EQ(
                SDDL2_op_segment_create_tagged(
                        stack_, &buffer, &segments, &registry),
                SDDL2_OK);
    }

    // Verify all 100 segments were created (no merging)
    EXPECT_EQ(segments.count, (size_t)NUM_SEGMENTS);

    // Verify capacity grew beyond initial (proves dynamic growth works)
    EXPECT_GT(segments.capacity, initial_capacity);
    EXPECT_GE(segments.capacity, (size_t)NUM_SEGMENTS);

    // Verify each segment is correct and in order
    for (int i = 0; i < NUM_SEGMENTS; i++) {
        EXPECT_EQ(segments.items[i].tag, (uint32_t)(1000 + i));
        EXPECT_EQ(segments.items[i].start_pos, (size_t)(i * SEGMENT_SIZE));
        EXPECT_EQ(segments.items[i].size_bytes, SEGMENT_SIZE);
        EXPECT_EQ(segments.items[i].type.kind, SDDL2_TYPE_U8);
        EXPECT_EQ(segments.items[i].type.width, 1u);
    }

    // Verify buffer cursor advanced correctly
    EXPECT_EQ(buffer.current_pos, (size_t)(NUM_SEGMENTS * SEGMENT_SIZE));

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2TaggedSegmentsLargeTest, SegmentListGrowthDifferentTypes)
{
    const int NUM_SEGMENT_GROUPS = (SDDL2_SEGMENT_INITIAL_CAPACITY + 4) / 4;
    const int NUM_SEGMENTS       = NUM_SEGMENT_GROUPS * 4;
    const int SEGMENT_GROUP_SIZE = 16;

    std::vector<uint8_t> data(NUM_SEGMENT_GROUPS * SEGMENT_GROUP_SIZE);
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;

    SDDL2_Input_cursor_init(&buffer, data.data(), data.size());
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    size_t initial_capacity = segments.capacity;

    // Types to cycle through (different sizes)
    SDDL2_Type_kind types[] = {
        SDDL2_TYPE_U8,    // 1 byte
        SDDL2_TYPE_I16LE, // 2 bytes
        SDDL2_TYPE_F32LE, // 4 bytes
        SDDL2_TYPE_I64BE  // 8 bytes
    };
    size_t element_counts[] = { 2, 1, 1, 1 }; // Total per segment: 2, 2, 4, 8

    // Create NUM_SEGMENTS segments with DIFFERENT tags and DIFFERENT types
    for (int i = 0; i < NUM_SEGMENT_GROUPS; i++) {
        for (int type_idx = 0; type_idx < 4; type_idx++) {
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_tag((uint32_t)(100 + 4 * i + type_idx)));
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type(
                            SDDL2_Type{ .kind        = types[type_idx],
                                        .width       = 1,
                                        .struct_data = nullptr }));
            SDDL2_Stack_push(
                    stack_, SDDL2_Value_i64((int64_t)element_counts[type_idx]));

            ASSERT_EQ(
                    SDDL2_op_segment_create_tagged(
                            stack_, &buffer, &segments, &registry),
                    SDDL2_OK);
        }
    }

    // Verify all 50 segments were created (no merging - different tags!)
    EXPECT_EQ(segments.count, (size_t)NUM_SEGMENT_GROUPS * 4);

    // Verify capacity grew beyond initial
    EXPECT_GT(segments.capacity, initial_capacity);
    EXPECT_GE(segments.capacity, (size_t)NUM_SEGMENTS);

    // Verify each segment has correct type and tag
    size_t expected_pos = 0;
    for (int i = 0; i < NUM_SEGMENTS; i++) {
        int type_idx = i % 4;
        size_t expected_bytes =
                element_counts[type_idx] * getKindSize(types[type_idx]);

        EXPECT_EQ(segments.items[i].tag, (uint32_t)(100 + i));
        EXPECT_EQ(segments.items[i].type.kind, types[type_idx]);
        EXPECT_EQ(segments.items[i].type.width, 1u);
        EXPECT_EQ(segments.items[i].start_pos, expected_pos);
        EXPECT_EQ(segments.items[i].size_bytes, expected_bytes);

        expected_pos += expected_bytes;
    }

    // 50 tags should be registered (each segment uses unique tag)
    EXPECT_EQ(registry.count, (size_t)NUM_SEGMENTS);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}
