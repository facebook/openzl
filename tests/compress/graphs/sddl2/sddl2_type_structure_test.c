// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Tests for SDDL2 Structure Types
 *
 * This file tests the new structure type system, which allows
 * creating complex types by composing multiple member types.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "openzl/compress/graphs/sddl2/sddl2_vm.h"
#include "sddl2_test_framework.h"

/* ============================================================================
 * Test 1: Simple Structure Creation
 * ========================================================================= */

TEST(test_simple_structure_creation)
{
    // Create a structure: {U8, I16LE, I32LE}
    // Expected size: 1 + 2 + 4 = 7 bytes

    // Allocate structure data
    SDDL2_Struct_data* struct_data =
            malloc(sizeof(SDDL2_Struct_data) + 3 * sizeof(SDDL2_Type));
    assert(struct_data != NULL);

    // Initialize structure metadata
    struct_data->member_count     = 3;
    struct_data->total_size_bytes = 0; // Will compute below

    // Set up members
    struct_data->members[0] = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL };
    struct_data->members[1] = (SDDL2_Type){ SDDL2_TYPE_I16LE, 1, NULL };
    struct_data->members[2] = (SDDL2_Type){ SDDL2_TYPE_I32LE, 1, NULL };

    // Compute total size
    struct_data->total_size_bytes = SDDL2_Type_size(struct_data->members[0])
            + SDDL2_Type_size(struct_data->members[1])
            + SDDL2_Type_size(struct_data->members[2]);

    // Verify size
    assert(struct_data->total_size_bytes == 7);

    // Create the structure type
    SDDL2_Type my_struct = { .kind         = SDDL2_TYPE_STRUCTURE,
                             .width        = 1, // Single instance
                             .complex_data = struct_data };

    // Verify structure type properties
    assert(my_struct.kind == SDDL2_TYPE_STRUCTURE);
    assert(my_struct.width == 1);
    assert(my_struct.complex_data != NULL);

    // Verify member count
    SDDL2_Struct_data* data =
            (SDDL2_Struct_data*)my_struct.complex_data;
    assert(data->member_count == 3);
    assert(data->total_size_bytes == 7);

    // Cleanup
    free(struct_data);
}

/* ============================================================================
 * Test 2: Array of Structures
 * ========================================================================= */

TEST(test_array_of_structures)
{
    // Create structure: {U8, I32LE}
    // Size: 1 + 4 = 5 bytes
    // Then create array of 10 such structures
    // Total: 5 * 10 = 50 bytes

    SDDL2_Struct_data* struct_data =
            malloc(sizeof(SDDL2_Struct_data) + 2 * sizeof(SDDL2_Type));
    assert(struct_data != NULL);

    struct_data->member_count     = 2;
    struct_data->members[0]       = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL };
    struct_data->members[1]       = (SDDL2_Type){ SDDL2_TYPE_I32LE, 1, NULL };
    struct_data->total_size_bytes = 1 + 4; // = 5

    // Create array of 10 structures
    SDDL2_Type array_of_structs = { .kind         = SDDL2_TYPE_STRUCTURE,
                                    .width        = 10, // 10 instances
                                    .complex_data = struct_data };

    // Verify
    assert(array_of_structs.width == 10);
    SDDL2_Struct_data* data =
            (SDDL2_Struct_data*)array_of_structs.complex_data;
    assert(data->total_size_bytes == 5);

    // Total size should be: element_size * width = 5 * 10 = 50
    // (once we implement SDDL2_Type_size for structures)

    free(struct_data);
}

/* ============================================================================
 * Test 3: Structure with Arrays as Members
 * ========================================================================= */

TEST(test_structure_with_array_members)
{
    // Create structure: {U8, [I32LE × 10], I16LE}
    // Size: 1 + (4*10) + 2 = 43 bytes

    SDDL2_Struct_data* struct_data =
            malloc(sizeof(SDDL2_Struct_data) + 3 * sizeof(SDDL2_Type));
    assert(struct_data != NULL);

    struct_data->member_count = 3;
    struct_data->members[0] = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL }; // 1 byte
    struct_data->members[1] =
            (SDDL2_Type){ SDDL2_TYPE_I32LE, 10, NULL }; // 40 bytes
    struct_data->members[2] =
            (SDDL2_Type){ SDDL2_TYPE_I16LE, 1, NULL }; // 2 bytes

    // Compute size
    struct_data->total_size_bytes = SDDL2_Type_size(struct_data->members[0])
            + SDDL2_Type_size(struct_data->members[1])
            + SDDL2_Type_size(struct_data->members[2]);

    assert(struct_data->total_size_bytes == 43);

    SDDL2_Type my_struct = { .kind         = SDDL2_TYPE_STRUCTURE,
                             .width        = 1,
                             .complex_data = struct_data };

    // Verify member types
    SDDL2_Struct_data* data =
            (SDDL2_Struct_data*)my_struct.complex_data;
    assert(data->members[1].width == 10); // Array member has width 10

    free(struct_data);
}

/* ============================================================================
 * Test 4: Nested Structures
 * ========================================================================= */

TEST(test_nested_structures)
{
    // Create inner structure: {U8, I16LE}
    // Size: 1 + 2 = 3 bytes
    SDDL2_Struct_data* inner_data =
            malloc(sizeof(SDDL2_Struct_data) + 2 * sizeof(SDDL2_Type));
    assert(inner_data != NULL);

    inner_data->member_count     = 2;
    inner_data->members[0]       = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL };
    inner_data->members[1]       = (SDDL2_Type){ SDDL2_TYPE_I16LE, 1, NULL };
    inner_data->total_size_bytes = 1 + 2; // = 3

    SDDL2_Type inner_struct = { .kind         = SDDL2_TYPE_STRUCTURE,
                                .width        = 1,
                                .complex_data = inner_data };

    // Create outer structure: {U8, inner_struct, I32LE}
    // Size: 1 + 3 + 4 = 8 bytes
    SDDL2_Struct_data* outer_data =
            malloc(sizeof(SDDL2_Struct_data) + 3 * sizeof(SDDL2_Type));
    assert(outer_data != NULL);

    outer_data->member_count = 3;
    outer_data->members[0]   = (SDDL2_Type){ SDDL2_TYPE_U8, 1, NULL };
    outer_data->members[1]   = inner_struct; // Nested structure!
    outer_data->members[2]   = (SDDL2_Type){ SDDL2_TYPE_I32LE, 1, NULL };

    // For now, manually compute size
    // Once SDDL2_Type_size handles structures, this will work automatically
    outer_data->total_size_bytes = 1 + 3 + 4; // = 8

    SDDL2_Type outer_struct = { .kind         = SDDL2_TYPE_STRUCTURE,
                                .width        = 1,
                                .complex_data = outer_data };

    // Verify nested structure
    SDDL2_Struct_data* outer_sd =
            (SDDL2_Struct_data*)outer_struct.complex_data;
    assert(outer_sd->member_count == 3);
    assert(outer_sd->members[1].kind == SDDL2_TYPE_STRUCTURE);
    assert(outer_sd->members[1].complex_data != NULL);

    // Cleanup (in reverse order)
    free(outer_data);
    free(inner_data);
}

/* ============================================================================
 * Main Test Runner
 * ========================================================================= */

int main(void)
{
    return sddl2_run_all_tests();
}
