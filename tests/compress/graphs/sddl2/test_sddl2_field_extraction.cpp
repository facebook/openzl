// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * Tests for extracting field sizes and types from structure types.
 *
 * This test file verifies that:
 * - Field sizes are correctly extracted from flat structures
 * - Array fields within structures are handled properly
 * - Nested structures flatten correctly
 * - Field types preserve endianness
 * - SDDL2_Type_size() works correctly for structure types
 */

#include <cstdlib>

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2FieldExtractionTest : public SDDL2StackTest {};

// Helper: Create a structure type from member types
SDDL2_Type createStructureType(SDDL2_Type* members, size_t member_count)
{
    size_t total_size = 0;
    for (size_t i = 0; i < member_count; i++) {
        total_size += getTypeSize(members[i]);
    }

    size_t alloc_size =
            sizeof(SDDL2_Struct_data) + member_count * sizeof(SDDL2_Type);
    auto* struct_data = (SDDL2_Struct_data*)malloc(alloc_size);

    struct_data->member_count     = member_count;
    struct_data->total_size_bytes = total_size;
    for (size_t i = 0; i < member_count; i++) {
        struct_data->members[i] = members[i];
    }

    return { .kind        = SDDL2_TYPE_STRUCTURE,
             .width       = 1,
             .struct_data = struct_data };
}

// Helper: Recursively flatten structure types into primitive type kinds
void flattenFieldTypes(SDDL2_Type type, SDDL2_Type_kind* output, size_t* index)
{
    if (type.kind == SDDL2_TYPE_STRUCTURE) {
        SDDL2_Struct_data* data = type.struct_data;
        for (size_t i = 0; i < data->member_count; i++) {
            flattenFieldTypes(data->members[i], output, index);
        }
    } else {
        output[(*index)++] = type.kind;
    }
}

} // namespace

// ============================================================================
// Flat Structure Field Size Tests
// ============================================================================

TEST_F(SDDL2FieldExtractionTest, SimpleFlatStructure)
{
    // Structure: {U8, I16LE, I32LE} → total 7 bytes
    SDDL2_Type members[3] = {
        { .kind = SDDL2_TYPE_U8, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I16LE, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I32LE, .width = 1, .struct_data = nullptr },
    };

    SDDL2_Type struct_type  = createStructureType(members, 3);
    SDDL2_Struct_data* data = struct_type.struct_data;

    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->member_count, 3u);
    EXPECT_EQ(data->total_size_bytes, 7u);

    EXPECT_EQ(getTypeSize(data->members[0]), 1u);
    EXPECT_EQ(getTypeSize(data->members[1]), 2u);
    EXPECT_EQ(getTypeSize(data->members[2]), 4u);

    free(data);
}

TEST_F(SDDL2FieldExtractionTest, StructureWithArrayField)
{
    // Structure: {U8, [I32LE × 10], I16LE} → total 43 bytes
    SDDL2_Type members[3] = {
        { .kind = SDDL2_TYPE_U8, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I32LE, .width = 10, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I16LE, .width = 1, .struct_data = nullptr },
    };

    SDDL2_Type struct_type  = createStructureType(members, 3);
    SDDL2_Struct_data* data = struct_type.struct_data;

    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->member_count, 3u);
    EXPECT_EQ(data->total_size_bytes, 43u);

    EXPECT_EQ(getTypeSize(data->members[0]), 1u);
    EXPECT_EQ(getTypeSize(data->members[1]), 40u);
    EXPECT_EQ(getTypeSize(data->members[2]), 2u);

    free(data);
}

TEST_F(SDDL2FieldExtractionTest, AllPrimitiveTypes)
{
    // Structure: {U8, U16LE, F32BE, I64LE} → total 15 bytes
    SDDL2_Type members[4] = {
        { .kind = SDDL2_TYPE_U8, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_U16LE, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_F32BE, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I64LE, .width = 1, .struct_data = nullptr },
    };

    SDDL2_Type struct_type  = createStructureType(members, 4);
    SDDL2_Struct_data* data = struct_type.struct_data;

    EXPECT_EQ(data->total_size_bytes, 15u);

    size_t expected_sizes[] = { 1, 2, 4, 8 };
    for (size_t i = 0; i < 4; i++) {
        EXPECT_EQ(getTypeSize(data->members[i]), expected_sizes[i]);
    }

    free(data);
}

TEST_F(SDDL2FieldExtractionTest, MixedEndianness)
{
    // Structure: {U16LE, I32BE, F64LE, U32BE} → total 18 bytes
    SDDL2_Type members[4] = {
        { .kind = SDDL2_TYPE_U16LE, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I32BE, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_F64LE, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_U32BE, .width = 1, .struct_data = nullptr },
    };

    SDDL2_Type struct_type  = createStructureType(members, 4);
    SDDL2_Struct_data* data = struct_type.struct_data;

    EXPECT_EQ(data->total_size_bytes, 18u);

    free(data);
}

TEST_F(SDDL2FieldExtractionTest, LargeStructure)
{
    // Structure with 10 I32LE fields → total 40 bytes
    SDDL2_Type members[10];
    for (size_t i = 0; i < 10; i++) {
        members[i] = { .kind        = SDDL2_TYPE_I32LE,
                       .width       = 1,
                       .struct_data = nullptr };
    }

    SDDL2_Type struct_type  = createStructureType(members, 10);
    SDDL2_Struct_data* data = struct_type.struct_data;

    EXPECT_EQ(data->member_count, 10u);
    EXPECT_EQ(data->total_size_bytes, 40u);

    free(data);
}

TEST_F(SDDL2FieldExtractionTest, TypeSizeFunction)
{
    // Verify SDDL2_Type_size() for single instance and array of structures
    SDDL2_Type members[2] = {
        { .kind = SDDL2_TYPE_U8, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I32LE, .width = 1, .struct_data = nullptr },
    };

    SDDL2_Type struct_type = createStructureType(members, 2);
    EXPECT_EQ(getTypeSize(struct_type), 5u); // 1 + 4

    struct_type.width = 10;
    EXPECT_EQ(getTypeSize(struct_type), 50u); // 5 × 10

    free(struct_type.struct_data);
}

// ============================================================================
// Nested Structure Field Size Tests
// ============================================================================

TEST_F(SDDL2FieldExtractionTest, NestedStructure2Levels)
{
    // Inner: {I16LE, I32LE} → 6 bytes
    SDDL2_Type inner_members[2] = {
        { .kind = SDDL2_TYPE_I16LE, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I32LE, .width = 1, .struct_data = nullptr },
    };
    SDDL2_Type inner = createStructureType(inner_members, 2);

    // Outer: {U8, inner, F64BE} → 1 + 6 + 8 = 15 bytes
    SDDL2_Type outer_members[3] = {
        { .kind = SDDL2_TYPE_U8, .width = 1, .struct_data = nullptr },
        inner,
        { .kind = SDDL2_TYPE_F64BE, .width = 1, .struct_data = nullptr },
    };
    SDDL2_Type outer = createStructureType(outer_members, 3);

    SDDL2_Struct_data* outer_data = outer.struct_data;
    EXPECT_EQ(outer_data->member_count, 3u);
    EXPECT_EQ(outer_data->total_size_bytes, 15u);

    free(inner.struct_data);
    free(outer_data);
}

TEST_F(SDDL2FieldExtractionTest, NestedStructure3Levels)
{
    // Level 1: {I16LE, I32LE} → 6 bytes
    SDDL2_Type level1_members[2] = {
        { .kind = SDDL2_TYPE_I16LE, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I32LE, .width = 1, .struct_data = nullptr },
    };
    SDDL2_Type level1 = createStructureType(level1_members, 2);

    // Level 2: {U8, level1} → 1 + 6 = 7 bytes
    SDDL2_Type level2_members[2] = {
        { .kind = SDDL2_TYPE_U8, .width = 1, .struct_data = nullptr },
        level1,
    };
    SDDL2_Type level2 = createStructureType(level2_members, 2);

    // Level 3: {level2, F64BE} → 7 + 8 = 15 bytes
    SDDL2_Type level3_members[2] = {
        level2,
        { .kind = SDDL2_TYPE_F64BE, .width = 1, .struct_data = nullptr },
    };
    SDDL2_Type level3 = createStructureType(level3_members, 2);

    EXPECT_EQ(getTypeSize(level3), 15u);

    free(level1.struct_data);
    free(level2.struct_data);
    free(level3.struct_data);
}

TEST_F(SDDL2FieldExtractionTest, NestedStructureWithArrays)
{
    // Inner: {U8, [I32LE × 5]} → 1 + 20 = 21 bytes
    SDDL2_Type inner_members[2] = {
        { .kind = SDDL2_TYPE_U8, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I32LE, .width = 5, .struct_data = nullptr },
    };
    SDDL2_Type inner = createStructureType(inner_members, 2);

    // Outer: {I16LE, inner, F64BE} → 2 + 21 + 8 = 31 bytes
    SDDL2_Type outer_members[3] = {
        { .kind = SDDL2_TYPE_I16LE, .width = 1, .struct_data = nullptr },
        inner,
        { .kind = SDDL2_TYPE_F64BE, .width = 1, .struct_data = nullptr },
    };
    SDDL2_Type outer = createStructureType(outer_members, 3);

    EXPECT_EQ(getTypeSize(outer), 31u);

    free(inner.struct_data);
    free(outer.struct_data);
}

TEST_F(SDDL2FieldExtractionTest, MultipleNestedStructures)
{
    // StructA: {U8, I16LE} → 3 bytes
    SDDL2_Type a_members[2] = {
        { .kind = SDDL2_TYPE_U8, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I16LE, .width = 1, .struct_data = nullptr },
    };
    SDDL2_Type structA = createStructureType(a_members, 2);

    // StructB: {I32LE, F64BE} → 12 bytes
    SDDL2_Type b_members[2] = {
        { .kind = SDDL2_TYPE_I32LE, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_F64BE, .width = 1, .struct_data = nullptr },
    };
    SDDL2_Type structB = createStructureType(b_members, 2);

    // Outer: {structA, U32LE, structB} → 3 + 4 + 12 = 19 bytes
    SDDL2_Type outer_members[3] = {
        structA,
        { .kind = SDDL2_TYPE_U32LE, .width = 1, .struct_data = nullptr },
        structB,
    };
    SDDL2_Type outer = createStructureType(outer_members, 3);

    EXPECT_EQ(getTypeSize(outer), 19u);

    free(structA.struct_data);
    free(structB.struct_data);
    free(outer.struct_data);
}

// ============================================================================
// Field Type Extraction Tests
// ============================================================================

TEST_F(SDDL2FieldExtractionTest, FieldTypesFlatStructure)
{
    // {U8, I16LE, I32LE} → flattened: [U8, I16LE, I32LE]
    SDDL2_Type members[3] = {
        { .kind = SDDL2_TYPE_U8, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I16LE, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I32LE, .width = 1, .struct_data = nullptr },
    };
    SDDL2_Type struct_type = createStructureType(members, 3);

    SDDL2_Type_kind field_types[3];
    size_t index = 0;
    flattenFieldTypes(struct_type, field_types, &index);

    EXPECT_EQ(index, 3u);
    EXPECT_EQ(field_types[0], SDDL2_TYPE_U8);
    EXPECT_EQ(field_types[1], SDDL2_TYPE_I16LE);
    EXPECT_EQ(field_types[2], SDDL2_TYPE_I32LE);

    free(struct_type.struct_data);
}

TEST_F(SDDL2FieldExtractionTest, FieldTypesNestedStructure)
{
    // {U8, {I16LE, I32LE}, F64BE} → flattened: [U8, I16LE, I32LE, F64BE]
    SDDL2_Type inner_members[2] = {
        { .kind = SDDL2_TYPE_I16LE, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I32LE, .width = 1, .struct_data = nullptr },
    };
    SDDL2_Type inner = createStructureType(inner_members, 2);

    SDDL2_Type outer_members[3] = {
        { .kind = SDDL2_TYPE_U8, .width = 1, .struct_data = nullptr },
        inner,
        { .kind = SDDL2_TYPE_F64BE, .width = 1, .struct_data = nullptr },
    };
    SDDL2_Type outer = createStructureType(outer_members, 3);

    SDDL2_Type_kind field_types[4];
    size_t index = 0;
    flattenFieldTypes(outer, field_types, &index);

    EXPECT_EQ(index, 4u);
    EXPECT_EQ(field_types[0], SDDL2_TYPE_U8);
    EXPECT_EQ(field_types[1], SDDL2_TYPE_I16LE);
    EXPECT_EQ(field_types[2], SDDL2_TYPE_I32LE);
    EXPECT_EQ(field_types[3], SDDL2_TYPE_F64BE);

    free(inner.struct_data);
    free(outer.struct_data);
}

TEST_F(SDDL2FieldExtractionTest, FieldTypesWithArrays)
{
    // {U8, [I32LE × 10], I16LE} → flattened: [U8, I32LE, I16LE]
    SDDL2_Type members[3] = {
        { .kind = SDDL2_TYPE_U8, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I32LE, .width = 10, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I16LE, .width = 1, .struct_data = nullptr },
    };
    SDDL2_Type struct_type = createStructureType(members, 3);

    SDDL2_Type_kind field_types[3];
    size_t index = 0;
    flattenFieldTypes(struct_type, field_types, &index);

    EXPECT_EQ(index, 3u);
    EXPECT_EQ(field_types[0], SDDL2_TYPE_U8);
    EXPECT_EQ(field_types[1], SDDL2_TYPE_I32LE);
    EXPECT_EQ(field_types[2], SDDL2_TYPE_I16LE);

    free(struct_type.struct_data);
}

TEST_F(SDDL2FieldExtractionTest, FieldTypesSizesAlignment)
{
    // {U8, {I16LE, I32LE}, F64BE} → types: [U8, I16LE, I32LE, F64BE]
    //                                sizes: [1,  2,     4,     8]
    SDDL2_Type inner_members[2] = {
        { .kind = SDDL2_TYPE_I16LE, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I32LE, .width = 1, .struct_data = nullptr },
    };
    SDDL2_Type inner = createStructureType(inner_members, 2);

    SDDL2_Type outer_members[3] = {
        { .kind = SDDL2_TYPE_U8, .width = 1, .struct_data = nullptr },
        inner,
        { .kind = SDDL2_TYPE_F64BE, .width = 1, .struct_data = nullptr },
    };
    SDDL2_Type outer = createStructureType(outer_members, 3);

    SDDL2_Type_kind field_types[4];
    size_t type_index = 0;
    flattenFieldTypes(outer, field_types, &type_index);

    SDDL2_Type_kind expected_types[] = {
        SDDL2_TYPE_U8, SDDL2_TYPE_I16LE, SDDL2_TYPE_I32LE, SDDL2_TYPE_F64BE
    };
    size_t expected_sizes[] = { 1, 2, 4, 8 };

    ASSERT_EQ(type_index, 4u);
    for (size_t i = 0; i < 4; i++) {
        EXPECT_EQ(field_types[i], expected_types[i]);
        EXPECT_EQ(getKindSize(field_types[i]), expected_sizes[i]);
    }

    free(inner.struct_data);
    free(outer.struct_data);
}

TEST_F(SDDL2FieldExtractionTest, FieldTypesMixedEndianness)
{
    // {U16LE, I32BE, F64LE, U32BE} → preserves endianness
    SDDL2_Type members[4] = {
        { .kind = SDDL2_TYPE_U16LE, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_I32BE, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_F64LE, .width = 1, .struct_data = nullptr },
        { .kind = SDDL2_TYPE_U32BE, .width = 1, .struct_data = nullptr },
    };
    SDDL2_Type struct_type = createStructureType(members, 4);

    SDDL2_Type_kind field_types[4];
    size_t index = 0;
    flattenFieldTypes(struct_type, field_types, &index);

    EXPECT_EQ(index, 4u);
    EXPECT_EQ(field_types[0], SDDL2_TYPE_U16LE);
    EXPECT_EQ(field_types[1], SDDL2_TYPE_I32BE);
    EXPECT_EQ(field_types[2], SDDL2_TYPE_F64LE);
    EXPECT_EQ(field_types[3], SDDL2_TYPE_U32BE);

    free(struct_type.struct_data);
}
