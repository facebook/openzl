// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2BitwiseTest : public SDDL2StackTest {};
} // namespace

// ============================================================================
// Basic Operation Tests
// ============================================================================

TEST_F(SDDL2BitwiseTest, BitAndBasic)
{
    // 0xFF00 & 0x0FF0 = 0x0F00
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0xFF00)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0x0FF0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_bit_and(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0x0F00);
}

TEST_F(SDDL2BitwiseTest, BitOrBasic)
{
    // 0x00F0 | 0x0F00 = 0x0FF0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0x00F0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0x0F00)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_bit_or(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0x0FF0);
}

TEST_F(SDDL2BitwiseTest, BitXorBasic)
{
    // 0xAAAA ^ 0x5555 = 0xFFFF
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0xAAAA)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0x5555)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_bit_xor(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0xFFFF);
}

TEST_F(SDDL2BitwiseTest, BitNotBasic)
{
    // ~0 = -1 (all bits set)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_bit_not(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, -1);
}

// ============================================================================
// Type Mismatch Tests
// ============================================================================

TEST_F(SDDL2BitwiseTest, BitAndTypeMismatch)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(100)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_bit_and(stack_, NULL, 0), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2BitwiseTest, BitNotTypeMismatch)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(42)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_bit_not(stack_, NULL, 0), SDDL2_TYPE_MISMATCH);
}

// ============================================================================
// Stack Underflow Tests
// ============================================================================

TEST_F(SDDL2BitwiseTest, BitAndUnderflow)
{
    EXPECT_EQ(SDDL2_op_bit_and(stack_, NULL, 0), SDDL2_STACK_UNDERFLOW);

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_bit_and(stack_, NULL, 0), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2BitwiseTest, BitNotUnderflow)
{
    EXPECT_EQ(SDDL2_op_bit_not(stack_, NULL, 0), SDDL2_STACK_UNDERFLOW);
}

// ============================================================================
// 64-bit Boundary Tests
// ============================================================================

TEST_F(SDDL2BitwiseTest, BitNotInt64MaxMin)
{
    // ~INT64_MAX = INT64_MIN
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(INT64_MAX)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_bit_not(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, INT64_MIN);

    // ~INT64_MIN = INT64_MAX
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(INT64_MIN)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_bit_not(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, INT64_MAX);
}

TEST_F(SDDL2BitwiseTest, BitXorInt64MinMax)
{
    // INT64_MIN ^ INT64_MAX = -1
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(INT64_MIN)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(INT64_MAX)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_bit_xor(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, -1);
}
