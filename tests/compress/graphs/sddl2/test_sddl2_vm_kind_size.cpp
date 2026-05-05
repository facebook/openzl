// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * Unit tests for SDDL2_kind_size() function.
 * Verifies that all 24 type kinds return the correct size in bytes.
 */

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

// ============================================================================
// 1-byte types
// ============================================================================

TEST(SDDL2KindSizeTest, OneByteTypes)
{
    EXPECT_EQ(getKindSize(SDDL2_TYPE_BYTES), 1);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_U8), 1);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_I8), 1);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_F8), 1);
}

// ============================================================================
// 2-byte types
// ============================================================================

TEST(SDDL2KindSizeTest, TwoByteTypes)
{
    // Integers
    EXPECT_EQ(getKindSize(SDDL2_TYPE_U16LE), 2);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_U16BE), 2);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_I16LE), 2);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_I16BE), 2);

    // Floats
    EXPECT_EQ(getKindSize(SDDL2_TYPE_F16LE), 2);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_F16BE), 2);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_BF16LE), 2);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_BF16BE), 2);
}

// ============================================================================
// 4-byte types
// ============================================================================

TEST(SDDL2KindSizeTest, FourByteTypes)
{
    // Integers
    EXPECT_EQ(getKindSize(SDDL2_TYPE_U32LE), 4);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_U32BE), 4);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_I32LE), 4);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_I32BE), 4);

    // Floats
    EXPECT_EQ(getKindSize(SDDL2_TYPE_F32LE), 4);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_F32BE), 4);
}

// ============================================================================
// 8-byte types
// ============================================================================

TEST(SDDL2KindSizeTest, EightByteTypes)
{
    // Integers
    EXPECT_EQ(getKindSize(SDDL2_TYPE_U64LE), 8);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_U64BE), 8);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_I64LE), 8);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_I64BE), 8);

    // Floats
    EXPECT_EQ(getKindSize(SDDL2_TYPE_F64LE), 8);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_F64BE), 8);
}

// ============================================================================
// Invalid type
// ============================================================================

TEST(SDDL2KindSizeTest, InvalidTypeReturnsError)
{
    SDDL2_RESULT_OF(size_t) result = SDDL2_kind_size(SDDL2_Type_kind(9999));
    EXPECT_TRUE(SDDL2_isError(result));
    EXPECT_EQ(SDDL2_error(result), SDDL2_TYPE_MISMATCH);
}

TEST(SDDL2KindSizeTest, StructReturnsError)
{
    SDDL2_RESULT_OF(size_t) result = SDDL2_kind_size(SDDL2_TYPE_STRUCTURE);
    EXPECT_TRUE(SDDL2_isError(result));
    EXPECT_EQ(SDDL2_error(result), SDDL2_TYPE_MISMATCH);
}
