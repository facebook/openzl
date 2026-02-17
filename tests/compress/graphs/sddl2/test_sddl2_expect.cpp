// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <climits>

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2ExpectTest : public SDDL2StackTest {};
} // namespace

// ============================================================================
// expect_true Success Tests
// ============================================================================

TEST_F(SDDL2ExpectTest, ExpectTruePositiveValue)
{
    // Test: expect_true with positive non-zero value should succeed
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_OK);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);
}

TEST_F(SDDL2ExpectTest, ExpectTrueLargePositive)
{
    // Test: expect_true with large positive value should succeed
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(123456789)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_OK);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);
}

TEST_F(SDDL2ExpectTest, ExpectTrueNegativeValue)
{
    // Test: expect_true with negative value (non-zero) should succeed
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_OK);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);
}

TEST_F(SDDL2ExpectTest, ExpectTrueLargeNegative)
{
    // Test: expect_true with large negative value should succeed
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-999999)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_OK);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);
}

TEST_F(SDDL2ExpectTest, ExpectTrueInt64Max)
{
    // Test: expect_true with INT64_MAX should succeed
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(INT64_MAX)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_OK);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);
}

TEST_F(SDDL2ExpectTest, ExpectTrueInt64Min)
{
    // Test: expect_true with INT64_MIN (most negative) should succeed
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(INT64_MIN)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_OK);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);
}

// ============================================================================
// expect_true Failure Tests
// ============================================================================

TEST_F(SDDL2ExpectTest, ExpectTrueZeroFails)
{
    // Test: expect_true with zero should fail with VALIDATION_FAILED
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_VALIDATION_FAILED);
    // Stack state after error is implementation-defined, but value should be
    // consumed
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);
}

// ============================================================================
// expect_true Error Condition Tests
// ============================================================================

TEST_F(SDDL2ExpectTest, ExpectTrueStackUnderflow)
{
    // Test: expect_true on empty stack should fail with STACK_UNDERFLOW
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2ExpectTest, ExpectTrueTypeMismatchTag)
{
    // Test: expect_true with Tag instead of I64 should fail with TYPE_MISMATCH
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(100)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2ExpectTest, ExpectTrueTypeMismatchType)
{
    // Test: expect_true with Type instead of I64 should fail with TYPE_MISMATCH
    SDDL2_Type type = { .kind = SDDL2_TYPE_U8, .width = 1 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_TYPE_MISMATCH);
}

// ============================================================================
// expect_true Combined with Comparison Operations
// ============================================================================

TEST_F(SDDL2ExpectTest, ExpectTrueWithCmpEqSuccess)
{
    // Test: cmp.eq followed by expect_true (equal values)
    // 42 == 42 -> 1 -> expect_true succeeds
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_eq(stack_, NULL, 0), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_OK);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);
}

TEST_F(SDDL2ExpectTest, ExpectTrueWithCmpEqFailure)
{
    // Test: cmp.eq followed by expect_true (different values)
    // 42 == 99 -> 0 -> expect_true fails
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(99)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_eq(stack_, NULL, 0), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_VALIDATION_FAILED);
}

TEST_F(SDDL2ExpectTest, ExpectTrueWithCmpNeSuccess)
{
    // Test: cmp.ne followed by expect_true (different values)
    // 42 != 99 -> 1 -> expect_true succeeds
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(99)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_ne(stack_, NULL, 0), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_OK);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);
}

TEST_F(SDDL2ExpectTest, ExpectTrueWithCmpLtSuccess)
{
    // Test: cmp.lt followed by expect_true
    // 10 < 20 -> 1 -> expect_true succeeds
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(20)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_lt(stack_, NULL, 0), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_OK);
}

TEST_F(SDDL2ExpectTest, ExpectTrueWithCmpGtFailure)
{
    // Test: cmp.gt followed by expect_true (false condition)
    // 10 > 20 -> 0 -> expect_true fails
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(20)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_gt(stack_, NULL, 0), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_VALIDATION_FAILED);
}

// ============================================================================
// expect_true Combined with Logic Operations
// ============================================================================

TEST_F(SDDL2ExpectTest, ExpectTrueWithLogicNot)
{
    // Test: logic.not followed by expect_true (expect_false pattern)
    // push 0 -> not -> -1 -> expect_true succeeds
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_not(stack_, NULL, 0), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_OK);
}

TEST_F(SDDL2ExpectTest, ExpectTrueWithLogicAnd)
{
    // Test: logic.and followed by expect_true
    // 0xFF & 0x0F = 0x0F (non-zero) -> expect_true succeeds
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0xFF)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0x0F)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_and(stack_, NULL, 0), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_OK);
}

TEST_F(SDDL2ExpectTest, ExpectTrueWithLogicAndZeroResult)
{
    // Test: logic.and followed by expect_true (zero result)
    // 0xFF & 0x00 = 0 -> expect_true fails
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0xFF)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0x00)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_and(stack_, NULL, 0), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_VALIDATION_FAILED);
}

// ============================================================================
// Multiple expect_true Tests
// ============================================================================

TEST_F(SDDL2ExpectTest, MultipleExpectTrueAllPass)
{
    // Test: Multiple expect_true operations, all should succeed
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_OK);

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_OK);

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_OK);

    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);
}

TEST_F(SDDL2ExpectTest, MultipleExpectTrueFirstFails)
{
    // Test: First expect_true fails, subsequent ops not reached
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_VALIDATION_FAILED);
}
