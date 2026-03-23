// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <cstdint>

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2LogicTest : public SDDL2StackTest {};

} // namespace

// ============================================================================
// AND Operation Tests - Logical AND (returns 0 or 1)
// ============================================================================

TEST_F(SDDL2LogicTest, LogicAndTrueTrue)
{
    // Test: true && true = 1
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_and(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 1);
}

TEST_F(SDDL2LogicTest, LogicAndTrueFalse)
{
    // Test: true && false = 0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_and(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);
}

TEST_F(SDDL2LogicTest, LogicAndFalseTrue)
{
    // Test: false && true = 0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_and(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);
}

TEST_F(SDDL2LogicTest, LogicAndFalseFalse)
{
    // Test: false && false = 0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_and(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);
}

TEST_F(SDDL2LogicTest, LogicAndNonzeroValues)
{
    // Test: Any non-zero values are treated as true
    // 100 && 200 = 1
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(100)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(200)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_and(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 1);
}

TEST_F(SDDL2LogicTest, LogicAndNegativeValues)
{
    // Test: Negative values are treated as true
    // -1 && -2 = 1
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_and(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 1);
}

TEST_F(SDDL2LogicTest, LogicAndZeroWithNonzero)
{
    // Test: 0xFFFFFFFF && 0 = 0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0xFFFFFFFF)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_and(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);
}

// ============================================================================
// OR Operation Tests - Logical OR (returns 0 or 1)
// ============================================================================

TEST_F(SDDL2LogicTest, LogicOrTrueTrue)
{
    // Test: true || true = 1
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_or(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 1);
}

TEST_F(SDDL2LogicTest, LogicOrTrueFalse)
{
    // Test: true || false = 1
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_or(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 1);
}

TEST_F(SDDL2LogicTest, LogicOrFalseTrue)
{
    // Test: false || true = 1
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_or(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 1);
}

TEST_F(SDDL2LogicTest, LogicOrFalseFalse)
{
    // Test: false || false = 0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_or(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);
}

TEST_F(SDDL2LogicTest, LogicOrNonzeroValues)
{
    // Test: Any non-zero values are treated as true
    // 100 || 200 = 1
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(100)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(200)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_or(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 1);
}

TEST_F(SDDL2LogicTest, LogicOrZeroWithNonzero)
{
    // Test: 0 || 42 = 1
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_or(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 1);
}

TEST_F(SDDL2LogicTest, LogicOrNegativeValues)
{
    // Test: Negative values are treated as true
    // -1 || 0 = 1
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_or(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 1);
}

// ============================================================================
// XOR Operation Tests - Logical XOR (returns 0 or 1)
// ============================================================================

TEST_F(SDDL2LogicTest, LogicXorTrueTrue)
{
    // Test: true ^^ true = 0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_xor(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);
}

TEST_F(SDDL2LogicTest, LogicXorTrueFalse)
{
    // Test: true ^^ false = 1
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_xor(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 1);
}

TEST_F(SDDL2LogicTest, LogicXorFalseTrue)
{
    // Test: false ^^ true = 1
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_xor(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 1);
}

TEST_F(SDDL2LogicTest, LogicXorFalseFalse)
{
    // Test: false ^^ false = 0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_xor(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);
}

TEST_F(SDDL2LogicTest, LogicXorNonzeroValues)
{
    // Test: Both non-zero = false
    // 100 ^^ 200 = 0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(100)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(200)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_xor(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);
}

TEST_F(SDDL2LogicTest, LogicXorZeroWithNonzero)
{
    // Test: One true, one false = true
    // 0 ^^ 42 = 1
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_xor(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 1);
}

TEST_F(SDDL2LogicTest, LogicXorNegativeValues)
{
    // Test: Both negative (both true) = false
    // -1 ^^ -2 = 0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_xor(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);
}

// ============================================================================
// NOT Operation Tests - Logical NOT (returns 0 or 1)
// ============================================================================

TEST_F(SDDL2LogicTest, LogicNotZero)
{
    // Test: !0 = 1
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_not(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 1);
}

TEST_F(SDDL2LogicTest, LogicNotOne)
{
    // Test: !1 = 0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_not(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);
}

TEST_F(SDDL2LogicTest, LogicNotPositive)
{
    // Test: !42 = 0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_not(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);
}

TEST_F(SDDL2LogicTest, LogicNotNegative)
{
    // Test: !(-1) = 0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_not(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);
}

TEST_F(SDDL2LogicTest, LogicNotLargeValue)
{
    // Test: !(large value) = 0
    ASSERT_EQ(
            SDDL2_Stack_push(stack_, SDDL2_Value_i64(0x123456789ABCDEF0)),
            SDDL2_OK);
    ASSERT_EQ(SDDL2_op_not(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);
}

TEST_F(SDDL2LogicTest, LogicNotDoubleNegation)
{
    // Test: !!0 = 0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_not(stack_, NULL, 0), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_not(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);
}

TEST_F(SDDL2LogicTest, LogicNotDoubleNegationNonzero)
{
    // Test: !!42 = 1 (normalizes to boolean)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_not(stack_, NULL, 0), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_not(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 1);
}

// ============================================================================
// Error Condition Tests
// ============================================================================

TEST_F(SDDL2LogicTest, LogicAndStackUnderflow)
{
    // Empty stack
    EXPECT_EQ(SDDL2_op_and(stack_, NULL, 0), SDDL2_STACK_UNDERFLOW);

    // Only one value
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_and(stack_, NULL, 0), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2LogicTest, LogicOrStackUnderflow)
{
    // Empty stack
    EXPECT_EQ(SDDL2_op_or(stack_, NULL, 0), SDDL2_STACK_UNDERFLOW);

    // Only one value
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_or(stack_, NULL, 0), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2LogicTest, LogicXorStackUnderflow)
{
    // Empty stack
    EXPECT_EQ(SDDL2_op_xor(stack_, NULL, 0), SDDL2_STACK_UNDERFLOW);

    // Only one value
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_xor(stack_, NULL, 0), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2LogicTest, LogicNotStackUnderflow)
{
    // Empty stack
    EXPECT_EQ(SDDL2_op_not(stack_, NULL, 0), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2LogicTest, LogicAndTypeMismatch)
{
    // Push Tag instead of I64 for second operand
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(100)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_and(stack_, NULL, 0), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LogicTest, LogicOrTypeMismatch)
{
    // Push Type instead of I64 for first operand
    SDDL2_Type type = { .kind = SDDL2_TYPE_U8, .width = 1 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_or(stack_, NULL, 0), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LogicTest, LogicXorTypeMismatch)
{
    // Push two Tags instead of I64s
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(20)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_xor(stack_, NULL, 0), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LogicTest, LogicNotTypeMismatch)
{
    // Push Type instead of I64
    SDDL2_Type type = { .kind = SDDL2_TYPE_U8, .width = 1 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_not(stack_, NULL, 0), SDDL2_TYPE_MISMATCH);
}

// ============================================================================
// Combined Operations Tests
// ============================================================================

TEST_F(SDDL2LogicTest, LogicCombinedOperations)
{
    // Test: ((a && b) || c) ^^ d
    // a=1, b=0, c=1, d=0
    // a && b = 0
    // (a && b) || c = 1
    // ((a && b) || c) ^^ d = 1

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_and(stack_, NULL, 0), SDDL2_OK); // Result: 0

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_or(stack_, NULL, 0), SDDL2_OK); // Result: 1

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_xor(stack_, NULL, 0), SDDL2_OK); // Result: 1

    popAndVerifyI64(stack_, 1);
}

TEST_F(SDDL2LogicTest, LogicDeMorganLaw)
{
    // Test De Morgan's Law: !(a || b) == (!a && !b)
    // a=1, b=0
    // a || b = 1, !(a || b) = 0
    // !a = 0, !b = 1, (!a && !b) = 0

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_or(stack_, NULL, 0), SDDL2_OK);  // Result: 1
    ASSERT_EQ(SDDL2_op_not(stack_, NULL, 0), SDDL2_OK); // Result: 0

    popAndVerifyI64(stack_, 0);
}
