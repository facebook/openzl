// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * Unit tests for creating segments with structure types.
 *
 * Tests that segment.create_tagged works correctly with:
 * - Single structure instances
 * - Arrays of structures
 * - Multiple segments with different structure types
 * - Segment merging with same structure type
 */

#include <cstring>

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2StructureSegmentTest : public SDDL2StackTest {};
} // namespace

// ============================================================================
// Success Scenarios
// ============================================================================

TEST_F(SDDL2StructureSegmentTest, SingleStructureSegment)
{
    // Structure {U8, I16LE, I32LE} = 7 bytes, 1 instance
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Build structure type {U8, I16LE, I32LE}
    SDDL2_Type u8_type  = { .kind        = SDDL2_TYPE_U8,
                            .width       = 1,
                            .struct_data = nullptr };
    SDDL2_Type i16_type = { .kind        = SDDL2_TYPE_I16LE,
                            .width       = 1,
                            .struct_data = nullptr };
    SDDL2_Type i32_type = { .kind        = SDDL2_TYPE_I32LE,
                            .width       = 1,
                            .struct_data = nullptr };

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(u8_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(i16_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(i32_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_structure(stack_, alloc_fn, alloc_ctx_), SDDL2_OK);

    // Pop the structure type, then push tag/type/size for segment creation
    SDDL2_Value struct_val;
    ASSERT_EQ(popValue(stack_, &struct_val), SDDL2_OK);

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(100)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, struct_val), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);

    ASSERT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_OK);

    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(segments.items[0].tag, 100u);
    EXPECT_EQ(segments.items[0].start_pos, 0u);
    EXPECT_EQ(segments.items[0].size_bytes, 7u);
    EXPECT_EQ(segments.items[0].type.kind, SDDL2_TYPE_STRUCTURE);
    EXPECT_EQ(segments.items[0].type.width, 1u);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2StructureSegmentTest, ArrayOfStructuresSegment)
{
    // Structure {U8, I32LE} = 5 bytes, array of 10 = 50 bytes
    uint8_t data[50];
    memset(data, 0x42, sizeof(data));
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Build structure {U8, I32LE}
    SDDL2_Type u8_type  = { .kind        = SDDL2_TYPE_U8,
                            .width       = 1,
                            .struct_data = nullptr };
    SDDL2_Type i32_type = { .kind        = SDDL2_TYPE_I32LE,
                            .width       = 1,
                            .struct_data = nullptr };

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(u8_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(i32_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_type_structure(stack_, alloc_fn, alloc_ctx_), SDDL2_OK);

    // Create array of 10 structures
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_type_fixed_array(stack_), SDDL2_OK);

    SDDL2_Value array_val;
    ASSERT_EQ(popValue(stack_, &array_val), SDDL2_OK);
    ASSERT_EQ(array_val.value.as_type.width, 10u);

    // Push tag/type/size for segment creation
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(200)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, array_val), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);

    ASSERT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_OK);

    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(segments.items[0].tag, 200u);
    EXPECT_EQ(segments.items[0].start_pos, 0u);
    EXPECT_EQ(segments.items[0].size_bytes, 50u); // 10 × 5
    EXPECT_EQ(segments.items[0].type.kind, SDDL2_TYPE_STRUCTURE);
    EXPECT_EQ(segments.items[0].type.width, 10u);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2StructureSegmentTest, MultipleStructureSegments)
{
    uint8_t data[100];
    memset(data, 0, sizeof(data));
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    SDDL2_Type u8_type  = { .kind        = SDDL2_TYPE_U8,
                            .width       = 1,
                            .struct_data = nullptr };
    SDDL2_Type i32_type = { .kind        = SDDL2_TYPE_I32LE,
                            .width       = 1,
                            .struct_data = nullptr };

    // Segment 1: {U8, U8} = 2 bytes, 10 instances = 20 bytes
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(u8_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(u8_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_type_structure(stack_, alloc_fn, alloc_ctx_), SDDL2_OK);

    SDDL2_Value struct1_val;
    ASSERT_EQ(popValue(stack_, &struct1_val), SDDL2_OK);

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(100)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, struct1_val), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);

    ASSERT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_OK);

    // Segment 2: {I32LE, I32LE} = 8 bytes, 5 instances = 40 bytes
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(i32_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(i32_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_type_structure(stack_, alloc_fn, alloc_ctx_), SDDL2_OK);

    SDDL2_Value struct2_val;
    ASSERT_EQ(popValue(stack_, &struct2_val), SDDL2_OK);

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(200)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, struct2_val), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);

    ASSERT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_OK);

    ASSERT_EQ(segments.count, 2u);

    EXPECT_EQ(segments.items[0].tag, 100u);
    EXPECT_EQ(segments.items[0].start_pos, 0u);
    EXPECT_EQ(segments.items[0].size_bytes, 20u); // 10 × 2
    EXPECT_EQ(segments.items[0].type.kind, SDDL2_TYPE_STRUCTURE);

    EXPECT_EQ(segments.items[1].tag, 200u);
    EXPECT_EQ(segments.items[1].start_pos, 20u);
    EXPECT_EQ(segments.items[1].size_bytes, 40u); // 5 × 8
    EXPECT_EQ(segments.items[1].type.kind, SDDL2_TYPE_STRUCTURE);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}

TEST_F(SDDL2StructureSegmentTest, SegmentMergingSameStructure)
{
    uint8_t data[100];
    memset(data, 0, sizeof(data));
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    SDDL2_Segment_list segments;
    SDDL2_Tag_registry registry;
    SDDL2_Segment_list_init(&segments, alloc_fn, alloc_ctx_);
    SDDL2_Tag_registry_init(&registry, alloc_fn, alloc_ctx_);

    // Structure {U8, I16LE} = 3 bytes
    SDDL2_Type u8_type  = { .kind        = SDDL2_TYPE_U8,
                            .width       = 1,
                            .struct_data = nullptr };
    SDDL2_Type i16_type = { .kind        = SDDL2_TYPE_I16LE,
                            .width       = 1,
                            .struct_data = nullptr };

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(u8_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(i16_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_type_structure(stack_, alloc_fn, alloc_ctx_), SDDL2_OK);

    SDDL2_Value struct_val;
    ASSERT_EQ(popValue(stack_, &struct_val), SDDL2_OK);

    // First segment: tag=100, 5 instances = 15 bytes
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(100)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, struct_val), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);

    ASSERT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_OK);
    ASSERT_EQ(segments.count, 1u);

    // Second segment with SAME tag and type: should merge
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(100)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, struct_val), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);

    ASSERT_EQ(
            SDDL2_op_segment_create_tagged(
                    stack_, &buffer, &segments, &registry),
            SDDL2_OK);

    // Should still be 1 segment (merged)
    EXPECT_EQ(segments.count, 1u);
    EXPECT_EQ(segments.items[0].tag, 100u);
    EXPECT_EQ(segments.items[0].start_pos, 0u);
    EXPECT_EQ(segments.items[0].size_bytes, 24u); // 8 instances × 3 bytes
    EXPECT_EQ(segments.items[0].type.kind, SDDL2_TYPE_STRUCTURE);

    SDDL2_Segment_list_destroy(&segments);
    SDDL2_Tag_registry_destroy(&registry);
}
