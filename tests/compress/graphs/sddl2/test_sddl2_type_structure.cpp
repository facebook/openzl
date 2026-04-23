// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * Unit tests for SDDL2 structure types.
 *
 * Tests structure type creation and properties:
 * - Simple structure creation and member verification
 * - Arrays of structures
 * - Structures with array members
 * - Nested structures
 */

#include <cstdlib>

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2TypeStructureTest : public SDDL2StackTest {};
} // namespace

// ============================================================================
// Success Scenarios
// ============================================================================

TEST_F(SDDL2TypeStructureTest, SimpleStructureCreation)
{
    // Structure: {U8, I16LE, I32LE} -> size = 1 + 2 + 4 = 7
    auto* struct_data = (SDDL2_Struct_data*)malloc(
            sizeof(SDDL2_Struct_data) + 3 * sizeof(SDDL2_Type));
    ASSERT_NE(struct_data, nullptr);

    struct_data->member_count     = 3;
    struct_data->total_size_bytes = 0;

    struct_data->members[0] = SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                          .width       = 1,
                                          .struct_data = nullptr };
    struct_data->members[1] = SDDL2_Type{ .kind        = SDDL2_TYPE_I16LE,
                                          .width       = 1,
                                          .struct_data = nullptr };
    struct_data->members[2] = SDDL2_Type{ .kind        = SDDL2_TYPE_I32LE,
                                          .width       = 1,
                                          .struct_data = nullptr };

    struct_data->total_size_bytes = getTypeSize(struct_data->members[0])
            + getTypeSize(struct_data->members[1])
            + getTypeSize(struct_data->members[2]);

    EXPECT_EQ(struct_data->total_size_bytes, 7);

    SDDL2_Type my_struct = { .kind        = SDDL2_TYPE_STRUCTURE,
                             .width       = 1,
                             .struct_data = struct_data };

    EXPECT_EQ(my_struct.kind, SDDL2_TYPE_STRUCTURE);
    EXPECT_EQ(my_struct.width, 1);
    ASSERT_NE(my_struct.struct_data, nullptr);

    SDDL2_Struct_data* data = my_struct.struct_data;
    EXPECT_EQ(data->member_count, 3);
    EXPECT_EQ(data->total_size_bytes, 7);

    free(struct_data);
}

TEST_F(SDDL2TypeStructureTest, ArrayOfStructures)
{
    // Structure: {U8, I32LE} -> size = 1 + 4 = 5
    // Array of 10 -> total = 5 * 10 = 50
    auto* struct_data = (SDDL2_Struct_data*)malloc(
            sizeof(SDDL2_Struct_data) + 2 * sizeof(SDDL2_Type));
    ASSERT_NE(struct_data, nullptr);

    struct_data->member_count     = 2;
    struct_data->members[0]       = SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                                .width       = 1,
                                                .struct_data = nullptr };
    struct_data->members[1]       = SDDL2_Type{ .kind        = SDDL2_TYPE_I32LE,
                                                .width       = 1,
                                                .struct_data = nullptr };
    struct_data->total_size_bytes = 1 + 4;

    SDDL2_Type array_of_structs = { .kind        = SDDL2_TYPE_STRUCTURE,
                                    .width       = 10,
                                    .struct_data = struct_data };

    EXPECT_EQ(array_of_structs.width, 10);
    EXPECT_EQ(array_of_structs.struct_data->total_size_bytes, 5);

    free(struct_data);
}

TEST_F(SDDL2TypeStructureTest, StructureWithArrayMembers)
{
    // Structure: {U8, [I32LE × 10], I16LE} -> 1 + (4*10) + 2 = 43
    auto* struct_data = (SDDL2_Struct_data*)malloc(
            sizeof(SDDL2_Struct_data) + 3 * sizeof(SDDL2_Type));
    ASSERT_NE(struct_data, nullptr);

    struct_data->member_count = 3;
    struct_data->members[0]   = SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                            .width       = 1,
                                            .struct_data = nullptr };
    struct_data->members[1]   = SDDL2_Type{ .kind        = SDDL2_TYPE_I32LE,
                                            .width       = 10,
                                            .struct_data = nullptr };
    struct_data->members[2]   = SDDL2_Type{ .kind        = SDDL2_TYPE_I16LE,
                                            .width       = 1,
                                            .struct_data = nullptr };

    struct_data->total_size_bytes = getTypeSize(struct_data->members[0])
            + getTypeSize(struct_data->members[1])
            + getTypeSize(struct_data->members[2]);

    EXPECT_EQ(struct_data->total_size_bytes, 43);

    SDDL2_Type my_struct = { .kind        = SDDL2_TYPE_STRUCTURE,
                             .width       = 1,
                             .struct_data = struct_data };

    EXPECT_EQ(my_struct.struct_data->members[1].width, 10);

    free(struct_data);
}

TEST_F(SDDL2TypeStructureTest, NestedStructures)
{
    // Inner: {U8, I16LE} -> 1 + 2 = 3
    auto* inner_data = (SDDL2_Struct_data*)malloc(
            sizeof(SDDL2_Struct_data) + 2 * sizeof(SDDL2_Type));
    ASSERT_NE(inner_data, nullptr);

    inner_data->member_count     = 2;
    inner_data->members[0]       = SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                               .width       = 1,
                                               .struct_data = nullptr };
    inner_data->members[1]       = SDDL2_Type{ .kind        = SDDL2_TYPE_I16LE,
                                               .width       = 1,
                                               .struct_data = nullptr };
    inner_data->total_size_bytes = 1 + 2;

    SDDL2_Type inner_struct = { .kind        = SDDL2_TYPE_STRUCTURE,
                                .width       = 1,
                                .struct_data = inner_data };

    // Outer: {U8, inner_struct, I32LE} -> 1 + 3 + 4 = 8
    auto* outer_data = (SDDL2_Struct_data*)malloc(
            sizeof(SDDL2_Struct_data) + 3 * sizeof(SDDL2_Type));
    ASSERT_NE(outer_data, nullptr);

    outer_data->member_count     = 3;
    outer_data->members[0]       = SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                               .width       = 1,
                                               .struct_data = nullptr };
    outer_data->members[1]       = inner_struct;
    outer_data->members[2]       = SDDL2_Type{ .kind        = SDDL2_TYPE_I32LE,
                                               .width       = 1,
                                               .struct_data = nullptr };
    outer_data->total_size_bytes = 1 + 3 + 4;

    SDDL2_Type outer_struct = { .kind        = SDDL2_TYPE_STRUCTURE,
                                .width       = 1,
                                .struct_data = outer_data };

    SDDL2_Struct_data* outer_sd = outer_struct.struct_data;
    EXPECT_EQ(outer_sd->member_count, 3);
    EXPECT_EQ(outer_sd->members[1].kind, SDDL2_TYPE_STRUCTURE);
    EXPECT_NE(outer_sd->members[1].struct_data, nullptr);

    free(outer_data);
    free(inner_data);
}
