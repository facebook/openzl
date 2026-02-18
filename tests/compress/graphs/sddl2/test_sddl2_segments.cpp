// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * Unit tests for OpenZL VM segment generation.
 * Tests Phase 4: Simple Byte Segments
 */

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2SegmentsTest : public SDDL2StackTest {};
} // namespace

// ============================================================================
// Segment List Init/Destroy Tests
// ============================================================================

TEST_F(SDDL2SegmentsTest, SegmentListInit)
{
    SDDL2_Segment_list list;
    SDDL2_Segment_list_init(&list, nullptr, nullptr);

    EXPECT_EQ(list.items, nullptr);
    EXPECT_EQ(list.count, 0u);
    EXPECT_EQ(list.capacity, 0u);
}

TEST_F(SDDL2SegmentsTest, SegmentListDestroy)
{
    SDDL2_Segment_list list;
    SDDL2_Segment_list_init(&list, alloc_fn, alloc_ctx_);
    SDDL2_Segment_list_destroy(&list);

    EXPECT_EQ(list.items, nullptr);
    EXPECT_EQ(list.count, 0u);
    EXPECT_EQ(list.capacity, 0u);
}

// ============================================================================
// Single Segment Tests
// ============================================================================

TEST_F(SDDL2SegmentsTest, CreateSingleSegment)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;

    SDDL2_Input_cursor_init(&buffer, data, 4);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);

    // Create unspecified segment: size=4 (no tag)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(4)), SDDL2_OK);
    ASSERT_EQ(
            SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments),
            SDDL2_OK);

    // Verify segment (tag=0 for unspecified)
    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(segments.items[0].tag, 0u);
    EXPECT_EQ(segments.items[0].start_pos, 0u);
    EXPECT_EQ(segments.items[0].size_bytes, 4u);

    // Verify cursor advanced
    EXPECT_EQ(buffer.current_pos, 4u);

    // Verify stack is empty
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0u);

    SDDL2_Segment_list_destroy(&segments);
}

TEST_F(SDDL2SegmentsTest, CreateZeroSizeSegment)
{
    uint8_t data[] = { 0xAA, 0xBB };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);

    // Create zero-size unspecified segment: size=0 (no tag)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(
            SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments),
            SDDL2_OK);

    // Verify segment (tag=0 for unspecified)
    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(segments.items[0].tag, 0u);
    EXPECT_EQ(segments.items[0].start_pos, 0u);
    EXPECT_EQ(segments.items[0].size_bytes, 0u);

    // Cursor should not advance
    EXPECT_EQ(buffer.current_pos, 0u);

    SDDL2_Segment_list_destroy(&segments);
}

// ============================================================================
// Bounds Checking Tests
// ============================================================================

TEST_F(SDDL2SegmentsTest, SegmentExceedsBuffer)
{
    uint8_t data[] = { 0x01, 0x02, 0x03 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;

    SDDL2_Input_cursor_init(&buffer, data, 3);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);

    // Try to create segment larger than buffer: size=10
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    EXPECT_EQ(
            SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments),
            SDDL2_SEGMENT_BOUNDS);

    // No segment should be created
    EXPECT_EQ(segments.count, 0u);

    // Cursor should not advance
    EXPECT_EQ(buffer.current_pos, 0u);

    SDDL2_Segment_list_destroy(&segments);
}

TEST_F(SDDL2SegmentsTest, SegmentAtExactBoundary)
{
    uint8_t data[] = { 0x10, 0x20, 0x30, 0x40, 0x50 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;

    SDDL2_Input_cursor_init(&buffer, data, 5);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);

    // Create segment exactly at buffer size: size=5
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    ASSERT_EQ(
            SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments),
            SDDL2_OK);

    // Verify segment
    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(segments.items[0].size_bytes, 5u);
    EXPECT_EQ(buffer.current_pos, 5u);

    // Try to create one more byte - should fail
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    EXPECT_EQ(
            SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments),
            SDDL2_SEGMENT_BOUNDS);

    SDDL2_Segment_list_destroy(&segments);
}

TEST_F(SDDL2SegmentsTest, SegmentAfterPartialConsumption)
{
    uint8_t data[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;

    SDDL2_Input_cursor_init(&buffer, data, 4);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);

    // Create first segment: size=2
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    ASSERT_EQ(
            SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments),
            SDDL2_OK);

    // Try to create segment that's too large for remaining: size=3 (only 2
    // left)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);
    EXPECT_EQ(
            SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments),
            SDDL2_SEGMENT_BOUNDS);

    // Only first segment should exist
    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(buffer.current_pos, 2u);

    SDDL2_Segment_list_destroy(&segments);
}

// ============================================================================
// Type Mismatch Tests
// ============================================================================

TEST_F(SDDL2SegmentsTest, SegmentWrongTagType)
{
    uint8_t data[] = { 0x01, 0x02 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);

    // Push Tag instead of I64 for size
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(100)), SDDL2_OK);
    EXPECT_EQ(
            SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments),
            SDDL2_TYPE_MISMATCH);

    EXPECT_EQ(segments.count, 0u);

    SDDL2_Segment_list_destroy(&segments);
}

TEST_F(SDDL2SegmentsTest, SegmentWrongSizeType)
{
    uint8_t data[] = { 0x01, 0x02 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);

    // Push Type instead of I64 for size
    SDDL2_Type t = { .kind = SDDL2_TYPE_U8, .width = 1 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(t)), SDDL2_OK);
    EXPECT_EQ(
            SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments),
            SDDL2_TYPE_MISMATCH);

    EXPECT_EQ(segments.count, 0u);

    SDDL2_Segment_list_destroy(&segments);
}

TEST_F(SDDL2SegmentsTest, SegmentNegativeTag)
{
    uint8_t data[] = { 0x01, 0x02 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);

    // Negative size should fail
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-100)), SDDL2_OK);
    EXPECT_EQ(
            SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments),
            SDDL2_TYPE_MISMATCH);

    EXPECT_EQ(segments.count, 0u);

    SDDL2_Segment_list_destroy(&segments);
}

TEST_F(SDDL2SegmentsTest, SegmentNegativeSize)
{
    uint8_t data[] = { 0x01, 0x02 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);

    // Another negative size test
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-5)), SDDL2_OK);
    EXPECT_EQ(
            SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments),
            SDDL2_TYPE_MISMATCH);

    EXPECT_EQ(segments.count, 0u);

    SDDL2_Segment_list_destroy(&segments);
}

// ============================================================================
// Stack Underflow Tests
// ============================================================================

TEST_F(SDDL2SegmentsTest, SegmentEmptyStack)
{
    uint8_t data[] = { 0x01, 0x02 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);

    // Empty stack - should underflow
    EXPECT_EQ(
            SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments),
            SDDL2_STACK_UNDERFLOW);

    EXPECT_EQ(segments.count, 0u);

    SDDL2_Segment_list_destroy(&segments);
}

TEST_F(SDDL2SegmentsTest, SegmentMissingSize)
{
    uint8_t data[] = { 0x01, 0x02 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);

    // Don't push anything - test empty stack
    EXPECT_EQ(
            SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments),
            SDDL2_STACK_UNDERFLOW);

    EXPECT_EQ(segments.count, 0u);

    SDDL2_Segment_list_destroy(&segments);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(SDDL2SegmentsTest, EndToEndSimpleProgram)
{
    uint8_t data[] = { 0x48, 0x65, 0x6C, 0x6C, 0x6F }; // "Hello"
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;

    SDDL2_Input_cursor_init(&buffer, data, 5);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);

    // Simulate VM program:
    // push 5            # size
    // segment_create_unspecified
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    ASSERT_EQ(
            SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments),
            SDDL2_OK);

    // Result: One unspecified segment covering entire "Hello" string
    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(segments.items[0].tag, 0u); // Unspecified
    EXPECT_EQ(segments.items[0].start_pos, 0u);
    EXPECT_EQ(segments.items[0].size_bytes, 5u);
    EXPECT_EQ(buffer.current_pos, 5u);

    SDDL2_Segment_list_destroy(&segments);
}

TEST_F(SDDL2SegmentsTest, SegmentsWithArithmetic)
{
    uint8_t data[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    SDDL2_Input_cursor buffer;
    SDDL2_Segment_list segments;

    SDDL2_Input_cursor_init(&buffer, data, 10);
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);

    // Simulate VM program with arithmetic:
    // push 2            # a
    // push 3            # b
    // add               # a + b = 5
    // segment_create_unspecified
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_add(stack_, NULL, 0), SDDL2_OK);
    ASSERT_EQ(
            SDDL2_op_segment_create_unspecified(stack_, &buffer, &segments),
            SDDL2_OK);

    // Result: One segment of size 5
    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(segments.items[0].size_bytes, 5u);

    SDDL2_Segment_list_destroy(&segments);
}
