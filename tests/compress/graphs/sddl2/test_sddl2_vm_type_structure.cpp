// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * Unit tests for SDDL2_op_type_structure() VM Operation.
 *
 * Tests the stack-based operation for creating structure types:
 * - Simple structure creation via VM op
 * - Structures with array members
 * - Array of structures (via type.fixed_array)
 * - Zero-member structures
 * - Error handling (negative members, wrong type)
 */

#include <cstdlib>

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2VmTypeStructureTest : public SDDL2StackTest {};
} // namespace

// ============================================================================
// Success Scenarios
// ============================================================================

TEST_F(SDDL2VmTypeStructureTest, SimpleStructure)
{
    // Push member types: U8, I16LE, I32LE
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

    // Push member count
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_structure(stack_, alloc_fn, alloc_ctx_), SDDL2_OK);

    EXPECT_EQ(SDDL2_Stack_depth(stack_), 1u);

    SDDL2_Value result{};
    ASSERT_EQ(popValue(stack_, &result), SDDL2_OK);
    EXPECT_EQ(result.kind, SDDL2_VALUE_TYPE);
    EXPECT_EQ(result.value.as_type.kind, SDDL2_TYPE_STRUCTURE);
    EXPECT_EQ(result.value.as_type.width, 1u);
    ASSERT_NE(result.value.as_type.struct_data, nullptr);

    SDDL2_Struct_data* struct_data = result.value.as_type.struct_data;
    EXPECT_EQ(struct_data->member_count, 3u);
    EXPECT_EQ(struct_data->total_size_bytes, 7u); // 1 + 2 + 4

    EXPECT_EQ(struct_data->members[0].kind, SDDL2_TYPE_U8);
    EXPECT_EQ(struct_data->members[1].kind, SDDL2_TYPE_I16LE);
    EXPECT_EQ(struct_data->members[2].kind, SDDL2_TYPE_I32LE);

    EXPECT_EQ(getTypeSize(result.value.as_type), 7u);
}

TEST_F(SDDL2VmTypeStructureTest, StructureWithArrays)
{
    // Structure {U8, [I32LE × 10], I16LE}
    SDDL2_Type u8_type        = { .kind        = SDDL2_TYPE_U8,
                                  .width       = 1,
                                  .struct_data = nullptr };
    SDDL2_Type i32_array_type = { .kind        = SDDL2_TYPE_I32LE,
                                  .width       = 10,
                                  .struct_data = nullptr };
    SDDL2_Type i16_type       = { .kind        = SDDL2_TYPE_I16LE,
                                  .width       = 1,
                                  .struct_data = nullptr };

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(u8_type)), SDDL2_OK);
    ASSERT_EQ(
            SDDL2_Stack_push(stack_, SDDL2_Value_type(i32_array_type)),
            SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(i16_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_structure(stack_, alloc_fn, alloc_ctx_), SDDL2_OK);

    SDDL2_Value result{};
    ASSERT_EQ(popValue(stack_, &result), SDDL2_OK);

    SDDL2_Struct_data* struct_data = result.value.as_type.struct_data;
    EXPECT_EQ(struct_data->total_size_bytes, 43u);
    EXPECT_EQ(struct_data->members[1].width, 10u);
}

TEST_F(SDDL2VmTypeStructureTest, ArrayOfStructures)
{
    // Create structure {U8, I32LE}, then array of 10
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

    // Use type.fixed_array to create array of 10 structures
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_type_fixed_array(stack_), SDDL2_OK);

    SDDL2_Value result{};
    ASSERT_EQ(popValue(stack_, &result), SDDL2_OK);
    EXPECT_EQ(result.value.as_type.kind, SDDL2_TYPE_STRUCTURE);
    EXPECT_EQ(result.value.as_type.width, 10u);

    SDDL2_Struct_data* struct_data = result.value.as_type.struct_data;
    EXPECT_EQ(struct_data->total_size_bytes, 5u); // Size of one instance

    // Total size = 5 bytes × 10 = 50 bytes
    EXPECT_EQ(getTypeSize(result.value.as_type), 50u);
}

TEST_F(SDDL2VmTypeStructureTest, ZeroMemberStructure)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_structure(stack_, alloc_fn, alloc_ctx_), SDDL2_OK);

    SDDL2_Value result{};
    ASSERT_EQ(popValue(stack_, &result), SDDL2_OK);
    EXPECT_EQ(result.kind, SDDL2_VALUE_TYPE);
    EXPECT_EQ(result.value.as_type.kind, SDDL2_TYPE_STRUCTURE);
    EXPECT_EQ(result.value.as_type.width, 1u);

    SDDL2_Struct_data* struct_data = result.value.as_type.struct_data;
    ASSERT_NE(struct_data, nullptr);
    EXPECT_EQ(struct_data->member_count, 0u);
    EXPECT_EQ(struct_data->total_size_bytes, 0u);
}

// ============================================================================
// Error Scenarios
// ============================================================================

TEST_F(SDDL2VmTypeStructureTest, ErrorNegativeMembers)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);

    EXPECT_EQ(
            SDDL2_op_type_structure(stack_, alloc_fn, alloc_ctx_),
            SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2VmTypeStructureTest, ErrorWrongTypeOnStack)
{
    // Push I64 instead of Type
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);

    EXPECT_EQ(
            SDDL2_op_type_structure(stack_, alloc_fn, alloc_ctx_),
            SDDL2_TYPE_MISMATCH);
}
