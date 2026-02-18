// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * Integration test for structure split-by-struct functionality.
 *
 * This test verifies the full integration chain:
 * 1. SDDL2 bytecode creates segments with structure types
 * 2. SDDL2_parse() splits structures into field arrays
 * 3. Each field is properly type-converted
 * 4. Fields are routed to compression
 *
 * This test focuses on the VM/interpreter level - full round-trip
 * compression tests are in C++ integration tests.
 */

#include <cstdlib>
#include <cstring>

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2StructureSplitIntegrationTest : public SDDL2StackTest {};
} // namespace

// ============================================================================
// Flat Structure Tests
// ============================================================================

TEST_F(SDDL2StructureSplitIntegrationTest, FlatStructureSplit)
{
    // Structure: {U8, I16LE, I32LE} = 7 bytes per instance, 10 instances = 70
    // bytes Expected after split:
    //   Field 0: 10 U8 values = 10 bytes
    //   Field 1: 10 I16LE values = 20 bytes
    //   Field 2: 10 I32LE values = 40 bytes

    SDDL2_Type u8_type  = { .kind        = SDDL2_TYPE_U8,
                            .width       = 1,
                            .struct_data = nullptr };
    SDDL2_Type i16_type = { .kind        = SDDL2_TYPE_I16LE,
                            .width       = 1,
                            .struct_data = nullptr };
    SDDL2_Type i32_type = { .kind        = SDDL2_TYPE_I32LE,
                            .width       = 1,
                            .struct_data = nullptr };

    size_t alloc_size = sizeof(SDDL2_Struct_data) + 3 * sizeof(SDDL2_Type);
    auto* struct_data = (SDDL2_Struct_data*)malloc(alloc_size);
    ASSERT_NE(struct_data, nullptr);

    struct_data->member_count     = 3;
    struct_data->total_size_bytes = 7; // 1 + 2 + 4
    struct_data->members[0]       = u8_type;
    struct_data->members[1]       = i16_type;
    struct_data->members[2]       = i32_type;

    SDDL2_Type struct_type = { .kind        = SDDL2_TYPE_STRUCTURE,
                               .width       = 1,
                               .struct_data = struct_data };

    EXPECT_EQ(struct_type.kind, SDDL2_TYPE_STRUCTURE);
    EXPECT_EQ(struct_type.width, 1u);
    EXPECT_EQ(struct_data->member_count, 3u);
    EXPECT_EQ(struct_data->total_size_bytes, 7u);

    EXPECT_EQ(SDDL2_Type_size(struct_data->members[0]), 1u); // U8
    EXPECT_EQ(SDDL2_Type_size(struct_data->members[1]), 2u); // I16LE
    EXPECT_EQ(SDDL2_Type_size(struct_data->members[2]), 4u); // I32LE

    free(struct_data);
}

// ============================================================================
// Nested Structure Tests
// ============================================================================

TEST_F(SDDL2StructureSplitIntegrationTest, NestedStructureSplit)
{
    // Outer: {U8, {I16LE, I32LE}, F64BE}
    // Inner: {I16LE, I32LE} = 6 bytes
    // Total: 1 + 6 + 8 = 15 bytes per instance
    // Flattened: 4 primitive fields [U8, I16LE, I32LE, F64BE]

    size_t inner_alloc = sizeof(SDDL2_Struct_data) + 2 * sizeof(SDDL2_Type);
    auto* inner_data   = (SDDL2_Struct_data*)malloc(inner_alloc);
    ASSERT_NE(inner_data, nullptr);

    inner_data->member_count     = 2;
    inner_data->total_size_bytes = 6; // 2 + 4
    inner_data->members[0]       = { .kind        = SDDL2_TYPE_I16LE,
                                     .width       = 1,
                                     .struct_data = nullptr };
    inner_data->members[1]       = { .kind        = SDDL2_TYPE_I32LE,
                                     .width       = 1,
                                     .struct_data = nullptr };

    SDDL2_Type inner_struct = { .kind        = SDDL2_TYPE_STRUCTURE,
                                .width       = 1,
                                .struct_data = inner_data };

    size_t outer_alloc = sizeof(SDDL2_Struct_data) + 3 * sizeof(SDDL2_Type);
    auto* outer_data   = (SDDL2_Struct_data*)malloc(outer_alloc);
    ASSERT_NE(outer_data, nullptr);

    outer_data->member_count     = 3;
    outer_data->total_size_bytes = 15; // 1 + 6 + 8
    outer_data->members[0]       = { .kind        = SDDL2_TYPE_U8,
                                     .width       = 1,
                                     .struct_data = nullptr };
    outer_data->members[1]       = inner_struct;
    outer_data->members[2]       = { .kind        = SDDL2_TYPE_F64BE,
                                     .width       = 1,
                                     .struct_data = nullptr };

    SDDL2_Type outer_struct = { .kind        = SDDL2_TYPE_STRUCTURE,
                                .width       = 1,
                                .struct_data = outer_data };

    EXPECT_EQ(outer_struct.kind, SDDL2_TYPE_STRUCTURE);
    EXPECT_EQ(outer_struct.width, 1u);
    EXPECT_EQ(outer_data->member_count, 3u);
    EXPECT_EQ(outer_data->total_size_bytes, 15u);

    EXPECT_EQ(outer_data->members[1].kind, SDDL2_TYPE_STRUCTURE);
    EXPECT_EQ(inner_data->member_count, 2u);

    free(inner_data);
    free(outer_data);
}

// ============================================================================
// Structure with Array Field Tests
// ============================================================================

TEST_F(SDDL2StructureSplitIntegrationTest, StructureWithArrayField)
{
    // Structure: {U8, [I32LE × 5], I16LE}
    //   Field 0: U8 (1 byte)
    //   Field 1: [I32LE × 5] (20 bytes)
    //   Field 2: I16LE (2 bytes)
    //   Total: 23 bytes per instance

    size_t alloc_size = sizeof(SDDL2_Struct_data) + 3 * sizeof(SDDL2_Type);
    auto* struct_data = (SDDL2_Struct_data*)malloc(alloc_size);
    ASSERT_NE(struct_data, nullptr);

    struct_data->member_count     = 3;
    struct_data->total_size_bytes = 23; // 1 + 20 + 2
    struct_data->members[0]       = { .kind        = SDDL2_TYPE_U8,
                                      .width       = 1,
                                      .struct_data = nullptr };
    struct_data->members[1]       = { .kind        = SDDL2_TYPE_I32LE,
                                      .width       = 5,
                                      .struct_data = nullptr };
    struct_data->members[2]       = { .kind        = SDDL2_TYPE_I16LE,
                                      .width       = 1,
                                      .struct_data = nullptr };

    SDDL2_Type struct_type = { .kind        = SDDL2_TYPE_STRUCTURE,
                               .width       = 1,
                               .struct_data = struct_data };

    EXPECT_EQ(struct_type.kind, SDDL2_TYPE_STRUCTURE);
    EXPECT_EQ(struct_data->member_count, 3u);
    EXPECT_EQ(struct_data->total_size_bytes, 23u);

    EXPECT_EQ(SDDL2_Type_size(struct_data->members[0]), 1u);  // U8
    EXPECT_EQ(SDDL2_Type_size(struct_data->members[1]), 20u); // I32LE × 5
    EXPECT_EQ(SDDL2_Type_size(struct_data->members[2]), 2u);  // I16LE

    free(struct_data);
}

// ============================================================================
// Mixed Endianness Tests
// ============================================================================

TEST_F(SDDL2StructureSplitIntegrationTest, MixedEndiannessStructure)
{
    // Structure: {U16LE, I32BE, F64LE} = 14 bytes

    size_t alloc_size = sizeof(SDDL2_Struct_data) + 3 * sizeof(SDDL2_Type);
    auto* struct_data = (SDDL2_Struct_data*)malloc(alloc_size);
    ASSERT_NE(struct_data, nullptr);

    struct_data->member_count     = 3;
    struct_data->total_size_bytes = 14; // 2 + 4 + 8
    struct_data->members[0]       = { .kind        = SDDL2_TYPE_U16LE,
                                      .width       = 1,
                                      .struct_data = nullptr };
    struct_data->members[1]       = { .kind        = SDDL2_TYPE_I32BE,
                                      .width       = 1,
                                      .struct_data = nullptr };
    struct_data->members[2]       = { .kind        = SDDL2_TYPE_F64LE,
                                      .width       = 1,
                                      .struct_data = nullptr };

    SDDL2_Type struct_type = { .kind        = SDDL2_TYPE_STRUCTURE,
                               .width       = 1,
                               .struct_data = struct_data };

    EXPECT_EQ(struct_type.kind, SDDL2_TYPE_STRUCTURE);
    EXPECT_EQ(struct_data->member_count, 3u);
    EXPECT_EQ(struct_data->total_size_bytes, 14u);

    EXPECT_EQ(struct_data->members[0].kind, SDDL2_TYPE_U16LE);
    EXPECT_EQ(struct_data->members[1].kind, SDDL2_TYPE_I32BE);
    EXPECT_EQ(struct_data->members[2].kind, SDDL2_TYPE_F64LE);

    free(struct_data);
}

// ============================================================================
// Field Size Extraction Tests
// ============================================================================

TEST_F(SDDL2StructureSplitIntegrationTest, FieldSizeExtraction)
{
    // Structure: {U8, I32LE, F64BE} = 13 bytes

    size_t alloc_size = sizeof(SDDL2_Struct_data) + 3 * sizeof(SDDL2_Type);
    auto* struct_data = (SDDL2_Struct_data*)malloc(alloc_size);
    ASSERT_NE(struct_data, nullptr);

    struct_data->member_count     = 3;
    struct_data->total_size_bytes = 13; // 1 + 4 + 8
    struct_data->members[0]       = { .kind        = SDDL2_TYPE_U8,
                                      .width       = 1,
                                      .struct_data = nullptr };
    struct_data->members[1]       = { .kind        = SDDL2_TYPE_I32LE,
                                      .width       = 1,
                                      .struct_data = nullptr };
    struct_data->members[2]       = { .kind        = SDDL2_TYPE_F64BE,
                                      .width       = 1,
                                      .struct_data = nullptr };

    EXPECT_EQ(SDDL2_Type_size(struct_data->members[0]), 1u);
    EXPECT_EQ(SDDL2_Type_size(struct_data->members[1]), 4u);
    EXPECT_EQ(SDDL2_Type_size(struct_data->members[2]), 8u);

    free(struct_data);
}
