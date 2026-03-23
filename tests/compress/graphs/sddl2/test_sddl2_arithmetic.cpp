// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <climits>

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2ArithmeticTest : public SDDL2StackTest {};
} // namespace

// ============================================================================
// Basic Operation Tests
// ============================================================================

TEST_F(SDDL2ArithmeticTest, AddBasic)
{
    // 5 + 3 = 8
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_add(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 8);
}

TEST_F(SDDL2ArithmeticTest, SubBasic)
{
    // 10 - 4 = 6
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(4)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_sub(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 6);
}

TEST_F(SDDL2ArithmeticTest, MulBasic)
{
    // 7 * 6 = 42
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(7)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(6)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_mul(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 42);
}

TEST_F(SDDL2ArithmeticTest, DivBasic)
{
    // 20 / 4 = 5
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(20)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(4)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_div(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 5);
}

TEST_F(SDDL2ArithmeticTest, ModBasic)
{
    // 17 % 5 = 2
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(17)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_mod(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 2);
}

TEST_F(SDDL2ArithmeticTest, AbsBasic)
{
    // abs(-42) = 42
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_abs(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 42);

    // abs(42) = 42
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_abs(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 42);
}

TEST_F(SDDL2ArithmeticTest, NegBasic)
{
    // -(-42) = 42
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_neg(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 42);

    // -(42) = -42
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_neg(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, -42);
}

// ============================================================================
// Overflow Detection Tests
// ============================================================================

TEST_F(SDDL2ArithmeticTest, AddOverflow)
{
    // INT64_MAX + 1 = overflow
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(INT64_MAX)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_add(stack_, NULL, 0), SDDL2_MATH_OVERFLOW);

    // INT64_MIN + (-1) = overflow
    SDDL2_Stack_init(stack_);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(INT64_MIN)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_add(stack_, NULL, 0), SDDL2_MATH_OVERFLOW);
}

TEST_F(SDDL2ArithmeticTest, SubOverflow)
{
    // INT64_MIN - 1 = overflow
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(INT64_MIN)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_sub(stack_, NULL, 0), SDDL2_MATH_OVERFLOW);

    // INT64_MAX - (-1) = overflow
    SDDL2_Stack_init(stack_);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(INT64_MAX)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_sub(stack_, NULL, 0), SDDL2_MATH_OVERFLOW);
}

TEST_F(SDDL2ArithmeticTest, MulOverflow)
{
    // INT64_MAX * 2 = overflow
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(INT64_MAX)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_mul(stack_, NULL, 0), SDDL2_MATH_OVERFLOW);

    // INT64_MIN * (-1) = overflow
    SDDL2_Stack_init(stack_);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(INT64_MIN)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_mul(stack_, NULL, 0), SDDL2_MATH_OVERFLOW);
}

TEST_F(SDDL2ArithmeticTest, AbsOverflow)
{
    // abs(INT64_MIN) = overflow
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(INT64_MIN)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_abs(stack_, NULL, 0), SDDL2_MATH_OVERFLOW);
}

TEST_F(SDDL2ArithmeticTest, NegOverflow)
{
    // -(INT64_MIN) = overflow
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(INT64_MIN)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_neg(stack_, NULL, 0), SDDL2_MATH_OVERFLOW);
}

// ============================================================================
// Divide-by-Zero Tests
// ============================================================================

TEST_F(SDDL2ArithmeticTest, DivByZero)
{
    // 10 / 0 = error
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_div(stack_, NULL, 0), SDDL2_DIV_ZERO);
}

TEST_F(SDDL2ArithmeticTest, ModByZero)
{
    // 10 % 0 = error
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_mod(stack_, NULL, 0), SDDL2_DIV_ZERO);
}

TEST_F(SDDL2ArithmeticTest, DivOverflowSpecial)
{
    // INT64_MIN / -1 = overflow
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(INT64_MIN)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_div(stack_, NULL, 0), SDDL2_MATH_OVERFLOW);
}

// ============================================================================
// Type Mismatch Tests
// ============================================================================

TEST_F(SDDL2ArithmeticTest, AddTypeMismatch)
{
    // I64 + Tag = type mismatch
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(100)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_add(stack_, NULL, 0), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2ArithmeticTest, MulTypeMismatch)
{
    // Tag * I64 = type mismatch
    SDDL2_Type t = { .kind = SDDL2_TYPE_I32LE, .width = 1 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(t)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_mul(stack_, NULL, 0), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2ArithmeticTest, AbsTypeMismatch)
{
    // abs(Tag) = type mismatch
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(42)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_abs(stack_, NULL, 0), SDDL2_TYPE_MISMATCH);
}

// ============================================================================
// Stack Underflow Tests
// ============================================================================

TEST_F(SDDL2ArithmeticTest, AddUnderflow)
{
    // Empty stack - should underflow
    EXPECT_EQ(SDDL2_op_add(stack_, NULL, 0), SDDL2_STACK_UNDERFLOW);

    // Only one operand - should underflow
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_add(stack_, NULL, 0), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2ArithmeticTest, NegUnderflow)
{
    // Empty stack - should underflow
    EXPECT_EQ(SDDL2_op_neg(stack_, NULL, 0), SDDL2_STACK_UNDERFLOW);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(SDDL2ArithmeticTest, ZeroOperations)
{
    // 0 + 0 = 0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_add(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);

    // 0 * 1000 = 0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1000)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_mul(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);

    // abs(0) = 0
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_abs(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 0);
}

TEST_F(SDDL2ArithmeticTest, NegativeOperations)
{
    // -5 + 3 = -2
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-5)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_add(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, -2);

    // -10 / -2 = 5
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_div(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 5);

    // -17 % 5 = -2 (C89/C99 behavior)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-17)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_mod(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, -2);
}
