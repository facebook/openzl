// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Unit tests for SDDL2_type_size() function.
 * Verifies that all 24 type kinds return the correct size in bytes.
 */

#include <assert.h>
#include <stdio.h>
#include "openzl/compress/graphs/sddl2/sddl2_vm.h"

/**
 * Test: All 1-byte types
 */
static void test_1byte_types(void)
{
    assert(SDDL2_kind_size(SDDL2_TYPE_BYTES) == 1);
    assert(SDDL2_kind_size(SDDL2_TYPE_U8) == 1);
    assert(SDDL2_kind_size(SDDL2_TYPE_I8) == 1);
    assert(SDDL2_kind_size(SDDL2_TYPE_F8) == 1);

    printf("✓ test_1byte_types passed (4 types)\n");
}

/**
 * Test: All 2-byte types
 */
static void test_2byte_types(void)
{
    // Integers
    assert(SDDL2_kind_size(SDDL2_TYPE_U16LE) == 2);
    assert(SDDL2_kind_size(SDDL2_TYPE_U16BE) == 2);
    assert(SDDL2_kind_size(SDDL2_TYPE_I16LE) == 2);
    assert(SDDL2_kind_size(SDDL2_TYPE_I16BE) == 2);

    // Floats
    assert(SDDL2_kind_size(SDDL2_TYPE_F16LE) == 2);
    assert(SDDL2_kind_size(SDDL2_TYPE_F16BE) == 2);
    assert(SDDL2_kind_size(SDDL2_TYPE_BF16LE) == 2);
    assert(SDDL2_kind_size(SDDL2_TYPE_BF16BE) == 2);

    printf("✓ test_2byte_types passed (8 types)\n");
}

/**
 * Test: All 4-byte types
 */
static void test_4byte_types(void)
{
    // Integers
    assert(SDDL2_kind_size(SDDL2_TYPE_U32LE) == 4);
    assert(SDDL2_kind_size(SDDL2_TYPE_U32BE) == 4);
    assert(SDDL2_kind_size(SDDL2_TYPE_I32LE) == 4);
    assert(SDDL2_kind_size(SDDL2_TYPE_I32BE) == 4);

    // Floats
    assert(SDDL2_kind_size(SDDL2_TYPE_F32LE) == 4);
    assert(SDDL2_kind_size(SDDL2_TYPE_F32BE) == 4);

    printf("✓ test_4byte_types passed (6 types)\n");
}

/**
 * Test: All 8-byte types
 */
static void test_8byte_types(void)
{
    // Integers
    assert(SDDL2_kind_size(SDDL2_TYPE_U64LE) == 8);
    assert(SDDL2_kind_size(SDDL2_TYPE_U64BE) == 8);
    assert(SDDL2_kind_size(SDDL2_TYPE_I64LE) == 8);
    assert(SDDL2_kind_size(SDDL2_TYPE_I64BE) == 8);

    // Floats
    assert(SDDL2_kind_size(SDDL2_TYPE_F64LE) == 8);
    assert(SDDL2_kind_size(SDDL2_TYPE_F64BE) == 8);

    printf("✓ test_8byte_types passed (6 types)\n");
}

/**
 * Test: Invalid type returns 0
 */
static void test_invalid_type(void)
{
    // Cast invalid enum value
    SDDL2_type_kind invalid = (SDDL2_type_kind)9999;
    assert(SDDL2_kind_size(invalid) == 0);

    printf("✓ test_invalid_type passed\n");
}

int main(void)
{
    printf("Running SDDL2_type_size() Tests...\n\n");

    test_1byte_types();
    test_2byte_types();
    test_4byte_types();
    test_8byte_types();
    test_invalid_type();

    printf("\n✅ All type size tests passed! (5 tests covering 24 types + invalid)\n");
    return 0;
}
