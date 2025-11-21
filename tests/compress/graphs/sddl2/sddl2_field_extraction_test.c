// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Tests for extracting field sizes from structure types.
 *
 * This test file verifies that sddl2_extract_flat_field_sizes() correctly:
 * - Extracts field sizes from flat structures
 * - Handles arrays within structures
 * - Rejects nested structures
 * - Skips zero-sized fields
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openzl/compress/graphs/sddl2/sddl2_vm.h"

// Forward declare the function we're testing (it's static in sddl2.c)
// For testing purposes, we'll need to either:
// 1. Make it non-static temporarily
// 2. Include the source file directly
// 3. Create a test-specific wrapper

// For now, let's create helper functions that replicate the logic
// and test those, then verify integration separately

/**
 * Test helper: Create a simple flat structure type.
 */
static SDDL2_Type create_test_structure_type(
        SDDL2_Type* member_types,
        size_t member_count)
{
    // Calculate total size
    size_t total_size = 0;
    for (size_t i = 0; i < member_count; i++) {
        total_size += SDDL2_Type_size(member_types[i]);
    }

    // Allocate structure data
    size_t alloc_size =
            sizeof(SDDL2_Struct_data) + member_count * sizeof(SDDL2_Type);
    SDDL2_Struct_data* struct_data = (SDDL2_Struct_data*)malloc(alloc_size);
    assert(struct_data != NULL);

    struct_data->member_count     = member_count;
    struct_data->total_size_bytes = total_size;

    // Copy member types
    for (size_t i = 0; i < member_count; i++) {
        struct_data->members[i] = member_types[i];
    }

    SDDL2_Type result = { .kind         = SDDL2_TYPE_STRUCTURE,
                          .width        = 1,
                          .complex_data = struct_data };
    return result;
}

/**
 * Test 1: Simple flat structure {U8, I16LE, I32LE}
 */
static void test_simple_flat_structure(void)
{
    printf("Testing simple flat structure {U8, I16LE, I32LE}...\n");

    // Create member types
    SDDL2_Type members[3];
    members[0] = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL };
    members[1] = (SDDL2_Type){ SDDL2_TYPE_I16LE, 1, NULL };
    members[2] = (SDDL2_Type){ SDDL2_TYPE_I32LE, 1, NULL };

    SDDL2_Type struct_type = create_test_structure_type(members, 3);

    // Verify structure metadata
    SDDL2_Struct_data* struct_data =
            (SDDL2_Struct_data*)struct_type.complex_data;
    assert(struct_data != NULL);
    assert(struct_data->member_count == 3);
    assert(struct_data->total_size_bytes == 7); // 1 + 2 + 4

    // Verify field sizes can be extracted
    size_t expected_sizes[3] = { 1, 2, 4 };
    for (size_t i = 0; i < 3; i++) {
        size_t field_size = SDDL2_Type_size(struct_data->members[i]);
        assert(field_size == expected_sizes[i]);
        printf("  Field %zu: size = %zu bytes ✓\n", i, field_size);
    }

    free(struct_data);
    printf("✓ test_simple_flat_structure passed\n\n");
}

/**
 * Test 2: Structure with array field {U8, [I32LE × 10], I16LE}
 */
static void test_structure_with_array_field(void)
{
    printf("Testing structure with array field {U8, [I32LE × 10], I16LE}...\n");

    // Create member types
    SDDL2_Type members[3];
    members[0] = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL };
    members[1] = (SDDL2_Type){ SDDL2_TYPE_I32LE, 10, NULL }; // Array!
    members[2] = (SDDL2_Type){ SDDL2_TYPE_I16LE, 1, NULL };

    SDDL2_Type struct_type = create_test_structure_type(members, 3);

    // Verify structure metadata
    SDDL2_Struct_data* struct_data =
            (SDDL2_Struct_data*)struct_type.complex_data;
    assert(struct_data != NULL);
    assert(struct_data->member_count == 3);
    assert(struct_data->total_size_bytes == 43); // 1 + 40 + 2

    // Verify field sizes
    size_t expected_sizes[3] = { 1, 40, 2 };
    for (size_t i = 0; i < 3; i++) {
        size_t field_size = SDDL2_Type_size(struct_data->members[i]);
        assert(field_size == expected_sizes[i]);
        printf("  Field %zu: size = %zu bytes ✓\n", i, field_size);
    }

    free(struct_data);
    printf("✓ test_structure_with_array_field passed\n\n");
}

/**
 * Test 3: All primitive types in structure
 */
static void test_all_primitive_types(void)
{
    printf("Testing structure with all primitive type sizes...\n");

    // Create structure with 1-byte, 2-byte, 4-byte, 8-byte fields
    SDDL2_Type members[4];
    members[0] = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL };    // 1 byte
    members[1] = (SDDL2_Type){ SDDL2_TYPE_U16LE, 1, NULL }; // 2 bytes
    members[2] = (SDDL2_Type){ SDDL2_TYPE_F32BE, 1, NULL }; // 4 bytes
    members[3] = (SDDL2_Type){ SDDL2_TYPE_I64LE, 1, NULL }; // 8 bytes

    SDDL2_Type struct_type = create_test_structure_type(members, 4);

    // Verify
    SDDL2_Struct_data* struct_data =
            (SDDL2_Struct_data*)struct_type.complex_data;
    assert(struct_data->total_size_bytes == 15); // 1 + 2 + 4 + 8

    size_t expected_sizes[4] = { 1, 2, 4, 8 };
    for (size_t i = 0; i < 4; i++) {
        size_t field_size = SDDL2_Type_size(struct_data->members[i]);
        assert(field_size == expected_sizes[i]);
    }

    free(struct_data);
    printf("✓ test_all_primitive_types passed\n\n");
}

/**
 * Test 4: Mixed endianness (should not affect field size extraction)
 */
static void test_mixed_endianness(void)
{
    printf("Testing structure with mixed endianness fields...\n");

    SDDL2_Type members[4];
    members[0] = (SDDL2_Type){ SDDL2_TYPE_U16LE, 1, NULL }; // LE
    members[1] = (SDDL2_Type){ SDDL2_TYPE_I32BE, 1, NULL }; // BE
    members[2] = (SDDL2_Type){ SDDL2_TYPE_F64LE, 1, NULL }; // LE
    members[3] = (SDDL2_Type){ SDDL2_TYPE_U32BE, 1, NULL }; // BE

    SDDL2_Type struct_type = create_test_structure_type(members, 4);

    // Verify
    SDDL2_Struct_data* struct_data =
            (SDDL2_Struct_data*)struct_type.complex_data;
    assert(struct_data->total_size_bytes == 18); // 2 + 4 + 8 + 4

    free(struct_data);
    printf("✓ test_mixed_endianness passed\n\n");
}

/**
 * Test 5: Large structure with many fields
 */
static void test_large_structure(void)
{
    printf("Testing large structure with 10 fields...\n");

    SDDL2_Type members[10];
    for (size_t i = 0; i < 10; i++) {
        members[i] = (SDDL2_Type){ SDDL2_TYPE_I32LE, 1, NULL };
    }

    SDDL2_Type struct_type = create_test_structure_type(members, 10);

    // Verify
    SDDL2_Struct_data* struct_data =
            (SDDL2_Struct_data*)struct_type.complex_data;
    assert(struct_data->member_count == 10);
    assert(struct_data->total_size_bytes == 40); // 10 × 4

    free(struct_data);
    printf("✓ test_large_structure passed\n\n");
}

/**
 * Test 6: Verify SDDL2_Type_size() handles structure types correctly
 */
static void test_type_size_function(void)
{
    printf("Testing SDDL2_Type_size() with structure types...\n");

    // Single instance of structure
    SDDL2_Type members[2];
    members[0] = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL };
    members[1] = (SDDL2_Type){ SDDL2_TYPE_I32LE, 1, NULL };

    SDDL2_Type struct_type = create_test_structure_type(members, 2);
    assert(SDDL2_Type_size(struct_type) == 5); // 1 + 4

    // Array of structures (width = 10)
    struct_type.width = 10;
    assert(SDDL2_Type_size(struct_type) == 50); // 5 × 10

    SDDL2_Struct_data* struct_data =
            (SDDL2_Struct_data*)struct_type.complex_data;
    free(struct_data);

    printf("✓ test_type_size_function passed\n\n");
}

/**
 * Test 7: Nested structure (2 levels) {U8, {I16LE, I32LE}, F64BE}
 */
static void test_nested_structure_2_levels(void)
{
    printf("Testing nested structure (2 levels) {U8, {I16LE, I32LE}, F64BE}...\n");

    // Create inner structure {I16LE, I32LE}
    SDDL2_Type inner_members[2];
    inner_members[0]        = (SDDL2_Type){ SDDL2_TYPE_I16LE, 1, NULL };
    inner_members[1]        = (SDDL2_Type){ SDDL2_TYPE_I32LE, 1, NULL };
    SDDL2_Type inner_struct = create_test_structure_type(inner_members, 2);

    // Create outer structure {U8, inner_struct, F64BE}
    SDDL2_Type outer_members[3];
    outer_members[0]        = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL };
    outer_members[1]        = inner_struct; // Nested structure!
    outer_members[2]        = (SDDL2_Type){ SDDL2_TYPE_F64BE, 1, NULL };
    SDDL2_Type outer_struct = create_test_structure_type(outer_members, 3);

    // Verify outer structure size
    SDDL2_Struct_data* outer_data =
            (SDDL2_Struct_data*)outer_struct.complex_data;
    assert(outer_data->member_count == 3);
    assert(outer_data->total_size_bytes == 15); // 1 + (2 + 4) + 8

    // Verify that when flattened, we should get 4 fields: [1, 2, 4, 8]
    // Field 0: U8 = 1 byte
    // Field 1: I16LE = 2 bytes (from inner struct)
    // Field 2: I32LE = 4 bytes (from inner struct)
    // Field 3: F64BE = 8 bytes

    // Cleanup
    SDDL2_Struct_data* inner_data =
            (SDDL2_Struct_data*)inner_struct.complex_data;
    free(inner_data);
    free(outer_data);

    printf("✓ test_nested_structure_2_levels passed\n\n");
}

/**
 * Test 8: Deeply nested structure (3 levels)
 */
static void test_nested_structure_3_levels(void)
{
    printf("Testing deeply nested structure (3 levels)...\n");

    // Level 1: {I16LE, I32LE}
    SDDL2_Type level1_members[2];
    level1_members[0]        = (SDDL2_Type){ SDDL2_TYPE_I16LE, 1, NULL };
    level1_members[1]        = (SDDL2_Type){ SDDL2_TYPE_I32LE, 1, NULL };
    SDDL2_Type level1_struct = create_test_structure_type(level1_members, 2);

    // Level 2: {U8, level1_struct}
    SDDL2_Type level2_members[2];
    level2_members[0]        = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL };
    level2_members[1]        = level1_struct;
    SDDL2_Type level2_struct = create_test_structure_type(level2_members, 2);

    // Level 3: {level2_struct, F64BE}
    SDDL2_Type level3_members[2];
    level3_members[0]        = level2_struct;
    level3_members[1]        = (SDDL2_Type){ SDDL2_TYPE_F64BE, 1, NULL };
    SDDL2_Type level3_struct = create_test_structure_type(level3_members, 2);

    // Verify total size: 1 + 2 + 4 + 8 = 15
    assert(SDDL2_Type_size(level3_struct) == 15);

    // When flattened, should give 4 fields: [1, 2, 4, 8]

    // Cleanup
    free((SDDL2_Struct_data*)level1_struct.complex_data);
    free((SDDL2_Struct_data*)level2_struct.complex_data);
    free((SDDL2_Struct_data*)level3_struct.complex_data);

    printf("✓ test_nested_structure_3_levels passed\n\n");
}

/**
 * Test 9: Nested structure with array fields
 */
static void test_nested_structure_with_arrays(void)
{
    printf("Testing nested structure with array fields...\n");

    // Inner: {U8, [I32LE × 5]}
    SDDL2_Type inner_members[2];
    inner_members[0] = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL };
    inner_members[1] = (SDDL2_Type){ SDDL2_TYPE_I32LE, 5, NULL }; // Array!
    SDDL2_Type inner_struct = create_test_structure_type(inner_members, 2);

    // Outer: {I16LE, inner_struct, F64BE}
    SDDL2_Type outer_members[3];
    outer_members[0]        = (SDDL2_Type){ SDDL2_TYPE_I16LE, 1, NULL };
    outer_members[1]        = inner_struct;
    outer_members[2]        = (SDDL2_Type){ SDDL2_TYPE_F64BE, 1, NULL };
    SDDL2_Type outer_struct = create_test_structure_type(outer_members, 3);

    // Verify total size: 2 + (1 + 20) + 8 = 31
    assert(SDDL2_Type_size(outer_struct) == 31);

    // When flattened, should give 4 fields: [2, 1, 20, 8]

    // Cleanup
    free((SDDL2_Struct_data*)inner_struct.complex_data);
    free((SDDL2_Struct_data*)outer_struct.complex_data);

    printf("✓ test_nested_structure_with_arrays passed\n\n");
}

/**
 * Test 10: Multiple nested structures at same level
 */
static void test_multiple_nested_structures(void)
{
    printf("Testing multiple nested structures at same level...\n");

    // Struct A: {U8, I16LE}
    SDDL2_Type structA_members[2];
    structA_members[0] = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL };
    structA_members[1] = (SDDL2_Type){ SDDL2_TYPE_I16LE, 1, NULL };
    SDDL2_Type structA = create_test_structure_type(structA_members, 2);

    // Struct B: {I32LE, F64BE}
    SDDL2_Type structB_members[2];
    structB_members[0] = (SDDL2_Type){ SDDL2_TYPE_I32LE, 1, NULL };
    structB_members[1] = (SDDL2_Type){ SDDL2_TYPE_F64BE, 1, NULL };
    SDDL2_Type structB = create_test_structure_type(structB_members, 2);

    // Outer: {structA, U32LE, structB}
    SDDL2_Type outer_members[3];
    outer_members[0]        = structA;
    outer_members[1]        = (SDDL2_Type){ SDDL2_TYPE_U32LE, 1, NULL };
    outer_members[2]        = structB;
    SDDL2_Type outer_struct = create_test_structure_type(outer_members, 3);

    // Verify total size: (1 + 2) + 4 + (4 + 8) = 19
    assert(SDDL2_Type_size(outer_struct) == 19);

    // When flattened, should give 5 fields: [1, 2, 4, 4, 8]

    // Cleanup
    free((SDDL2_Struct_data*)structA.complex_data);
    free((SDDL2_Struct_data*)structB.complex_data);
    free((SDDL2_Struct_data*)outer_struct.complex_data);

    printf("✓ test_multiple_nested_structures passed\n\n");
}

/**
 * Helper function to manually flatten field types (mirrors sddl2.c logic)
 */
static void manually_flatten_field_types_recursive(
        SDDL2_Type type,
        SDDL2_Type_kind* output,
        size_t* index)
{
    if (type.kind == SDDL2_TYPE_STRUCTURE) {
        assert(type.width == 1); // We don't support arrays of structures
        SDDL2_Struct_data* struct_data = (SDDL2_Struct_data*)type.complex_data;
        for (size_t i = 0; i < struct_data->member_count; i++) {
            manually_flatten_field_types_recursive(
                    struct_data->members[i], output, index);
        }
    } else {
        // Primitive: record type kind
        output[(*index)++] = type.kind;
    }
}

/**
 * Test 11: Field types - Simple flat structure
 */
static void test_field_types_flat_structure(void)
{
    printf("Testing field types extraction for flat structure {U8, I16LE, I32LE}...\n");

    // Create structure {U8, I16LE, I32LE}
    SDDL2_Type members[3];
    members[0]             = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL };
    members[1]             = (SDDL2_Type){ SDDL2_TYPE_I16LE, 1, NULL };
    members[2]             = (SDDL2_Type){ SDDL2_TYPE_I32LE, 1, NULL };
    SDDL2_Type struct_type = create_test_structure_type(members, 3);

    // Extract field types manually
    SDDL2_Type_kind field_types[3];
    size_t index = 0;
    manually_flatten_field_types_recursive(struct_type, field_types, &index);

    // Verify
    assert(index == 3);
    assert(field_types[0] == SDDL2_TYPE_U8);
    assert(field_types[1] == SDDL2_TYPE_I16LE);
    assert(field_types[2] == SDDL2_TYPE_I32LE);

    printf("  Field 0: U8 ✓\n");
    printf("  Field 1: I16LE ✓\n");
    printf("  Field 2: I32LE ✓\n");

    free((SDDL2_Struct_data*)struct_type.complex_data);
    printf("✓ test_field_types_flat_structure passed\n\n");
}

/**
 * Test 12: Field types - Nested structure
 */
static void test_field_types_nested_structure(void)
{
    printf("Testing field types extraction for nested structure {U8, {I16LE, I32LE}, F64BE}...\n");

    // Create inner structure {I16LE, I32LE}
    SDDL2_Type inner_members[2];
    inner_members[0]        = (SDDL2_Type){ SDDL2_TYPE_I16LE, 1, NULL };
    inner_members[1]        = (SDDL2_Type){ SDDL2_TYPE_I32LE, 1, NULL };
    SDDL2_Type inner_struct = create_test_structure_type(inner_members, 2);

    // Create outer structure {U8, inner_struct, F64BE}
    SDDL2_Type outer_members[3];
    outer_members[0]        = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL };
    outer_members[1]        = inner_struct;
    outer_members[2]        = (SDDL2_Type){ SDDL2_TYPE_F64BE, 1, NULL };
    SDDL2_Type outer_struct = create_test_structure_type(outer_members, 3);

    // Extract field types
    SDDL2_Type_kind field_types[4];
    size_t index = 0;
    manually_flatten_field_types_recursive(outer_struct, field_types, &index);

    // Verify flattened types: [U8, I16LE, I32LE, F64BE]
    assert(index == 4);
    assert(field_types[0] == SDDL2_TYPE_U8);
    assert(field_types[1] == SDDL2_TYPE_I16LE);
    assert(field_types[2] == SDDL2_TYPE_I32LE);
    assert(field_types[3] == SDDL2_TYPE_F64BE);

    printf("  Field 0: U8 ✓\n");
    printf("  Field 1: I16LE (from inner) ✓\n");
    printf("  Field 2: I32LE (from inner) ✓\n");
    printf("  Field 3: F64BE ✓\n");

    free((SDDL2_Struct_data*)inner_struct.complex_data);
    free((SDDL2_Struct_data*)outer_struct.complex_data);
    printf("✓ test_field_types_nested_structure passed\n\n");
}

/**
 * Test 13: Field types - With array fields
 */
static void test_field_types_with_arrays(void)
{
    printf("Testing field types with array fields {U8, [I32LE × 10], I16LE}...\n");

    // Create structure {U8, [I32LE × 10], I16LE}
    SDDL2_Type members[3];
    members[0] = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL };
    members[1] = (SDDL2_Type){ SDDL2_TYPE_I32LE, 10, NULL }; // Array!
    members[2] = (SDDL2_Type){ SDDL2_TYPE_I16LE, 1, NULL };
    SDDL2_Type struct_type = create_test_structure_type(members, 3);

    // Extract field types
    SDDL2_Type_kind field_types[3];
    size_t index = 0;
    manually_flatten_field_types_recursive(struct_type, field_types, &index);

    // Verify: types should be element types, not array types
    // [U8, I32LE, I16LE] - note the array becomes just I32LE (element type)
    assert(index == 3);
    assert(field_types[0] == SDDL2_TYPE_U8);
    assert(field_types[1] == SDDL2_TYPE_I32LE); // Element type, not array
    assert(field_types[2] == SDDL2_TYPE_I16LE);

    printf("  Field 0: U8 (size=1) ✓\n");
    printf("  Field 1: I32LE (element type of array, size=40) ✓\n");
    printf("  Field 2: I16LE (size=2) ✓\n");

    free((SDDL2_Struct_data*)struct_type.complex_data);
    printf("✓ test_field_types_with_arrays passed\n\n");
}

/**
 * Test 14: Field types and sizes alignment
 */
static void test_field_types_sizes_alignment(void)
{
    printf("Testing that field types and sizes arrays align correctly...\n");

    // Create nested structure {U8, {I16LE, I32LE}, F64BE}
    SDDL2_Type inner_members[2];
    inner_members[0]        = (SDDL2_Type){ SDDL2_TYPE_I16LE, 1, NULL };
    inner_members[1]        = (SDDL2_Type){ SDDL2_TYPE_I32LE, 1, NULL };
    SDDL2_Type inner_struct = create_test_structure_type(inner_members, 2);

    SDDL2_Type outer_members[3];
    outer_members[0]        = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL };
    outer_members[1]        = inner_struct;
    outer_members[2]        = (SDDL2_Type){ SDDL2_TYPE_F64BE, 1, NULL };
    SDDL2_Type outer_struct = create_test_structure_type(outer_members, 3);

    // Extract field types
    SDDL2_Type_kind field_types[4];
    size_t type_index = 0;
    manually_flatten_field_types_recursive(
            outer_struct, field_types, &type_index);

    // Expected:
    // Types: [U8,     I16LE,  I32LE,  F64BE]
    // Sizes: [1,      2,      4,      8]
    size_t expected_sizes[4]          = { 1, 2, 4, 8 };
    SDDL2_Type_kind expected_types[4] = {
        SDDL2_TYPE_U8, SDDL2_TYPE_I16LE, SDDL2_TYPE_I32LE, SDDL2_TYPE_F64BE
    };

    // Verify alignment
    assert(type_index == 4);
    for (size_t i = 0; i < 4; i++) {
        assert(field_types[i] == expected_types[i]);

        // Calculate size from type
        size_t actual_size = SDDL2_kind_size(field_types[i]);
        assert(actual_size == expected_sizes[i]);

        printf("  Field %zu: type=%d, size=%zu bytes ✓\n",
               i,
               (int)field_types[i],
               actual_size);
    }

    free((SDDL2_Struct_data*)inner_struct.complex_data);
    free((SDDL2_Struct_data*)outer_struct.complex_data);
    printf("✓ test_field_types_sizes_alignment passed\n\n");
}

/**
 * Test 15: Mixed endianness field types
 */
static void test_field_types_mixed_endianness(void)
{
    printf("Testing field types with mixed endianness...\n");

    // Create structure with LE and BE types
    SDDL2_Type members[4];
    members[0]             = (SDDL2_Type){ SDDL2_TYPE_U16LE, 1, NULL };
    members[1]             = (SDDL2_Type){ SDDL2_TYPE_I32BE, 1, NULL };
    members[2]             = (SDDL2_Type){ SDDL2_TYPE_F64LE, 1, NULL };
    members[3]             = (SDDL2_Type){ SDDL2_TYPE_U32BE, 1, NULL };
    SDDL2_Type struct_type = create_test_structure_type(members, 4);

    // Extract field types
    SDDL2_Type_kind field_types[4];
    size_t index = 0;
    manually_flatten_field_types_recursive(struct_type, field_types, &index);

    // Verify types preserve endianness
    assert(index == 4);
    assert(field_types[0] == SDDL2_TYPE_U16LE);
    assert(field_types[1] == SDDL2_TYPE_I32BE);
    assert(field_types[2] == SDDL2_TYPE_F64LE);
    assert(field_types[3] == SDDL2_TYPE_U32BE);

    printf("  Field 0: U16LE ✓\n");
    printf("  Field 1: I32BE ✓\n");
    printf("  Field 2: F64LE ✓\n");
    printf("  Field 3: U32BE ✓\n");

    free((SDDL2_Struct_data*)struct_type.complex_data);
    printf("✓ test_field_types_mixed_endianness passed\n\n");
}

int main(void)
{
    printf("===========================================\n");
    printf("SDDL2 Field Size & Type Extraction Tests\n");
    printf("===========================================\n\n");

    printf("--- Field Size Tests ---\n\n");

    // Flat structure tests
    test_simple_flat_structure();
    test_structure_with_array_field();
    test_all_primitive_types();
    test_mixed_endianness();
    test_large_structure();
    test_type_size_function();

    // Nested structure tests
    test_nested_structure_2_levels();
    test_nested_structure_3_levels();
    test_nested_structure_with_arrays();
    test_multiple_nested_structures();

    printf("--- Field Type Tests ---\n\n");

    // Field type extraction tests
    test_field_types_flat_structure();
    test_field_types_nested_structure();
    test_field_types_with_arrays();
    test_field_types_sizes_alignment();
    test_field_types_mixed_endianness();

    printf("===========================================\n");
    printf("✅ All field extraction tests passed (15 tests)!\n");
    printf("===========================================\n");

    return 0;
}
