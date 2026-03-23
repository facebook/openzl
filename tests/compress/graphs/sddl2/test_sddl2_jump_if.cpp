// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * Unit tests for the jump_if VM operation.
 * Tests conditionally skipping N instructions based on a condition value.
 */

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2JumpIfTest : public SDDL2StackTest {};
} // namespace

// ============================================================================
// Basic Jump Tests
// ============================================================================

TEST_F(SDDL2JumpIfTest, JumpIfTrueSkipsInstructions)
{
    // Push N=3 (skip count), then condition=1 (true)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);

    size_t skip_count = 0;
    ASSERT_EQ(SDDL2_op_jump_if(stack_, &skip_count), SDDL2_OK);

    EXPECT_EQ(skip_count, 3u);
    EXPECT_EQ(stack_->top, 0u);
}

TEST_F(SDDL2JumpIfTest, JumpIfFalseDoesNotSkip)
{
    // Push N=3 (skip count), then condition=0 (false)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);

    size_t skip_count = 999;
    ASSERT_EQ(SDDL2_op_jump_if(stack_, &skip_count), SDDL2_OK);

    EXPECT_EQ(skip_count, 0u);
    EXPECT_EQ(stack_->top, 0u);
}

TEST_F(SDDL2JumpIfTest, JumpIfWithNZero)
{
    // Push N=0 (skip nothing), then condition=1 (true)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);

    size_t skip_count = 999;
    ASSERT_EQ(SDDL2_op_jump_if(stack_, &skip_count), SDDL2_OK);

    EXPECT_EQ(skip_count, 0u);
    EXPECT_EQ(stack_->top, 0u);
}

TEST_F(SDDL2JumpIfTest, JumpIfNonZeroConditionIsTrue)
{
    // Push N=5, then condition=100 (non-zero = true)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(100)), SDDL2_OK);

    size_t skip_count = 0;
    ASSERT_EQ(SDDL2_op_jump_if(stack_, &skip_count), SDDL2_OK);

    EXPECT_EQ(skip_count, 5u);
    EXPECT_EQ(stack_->top, 0u);
}

TEST_F(SDDL2JumpIfTest, JumpIfNegativeConditionIsTrue)
{
    // Push N=2, then condition=-1 (negative = true)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);

    size_t skip_count = 0;
    ASSERT_EQ(SDDL2_op_jump_if(stack_, &skip_count), SDDL2_OK);

    EXPECT_EQ(skip_count, 2u);
    EXPECT_EQ(stack_->top, 0u);
}

// ============================================================================
// Error Cases
// ============================================================================

TEST_F(SDDL2JumpIfTest, JumpIfUnderflow)
{
    // Only condition, no N value
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);

    size_t skip_count = 0;
    EXPECT_EQ(SDDL2_op_jump_if(stack_, &skip_count), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2JumpIfTest, JumpIfTypeMismatchCondition)
{
    // Push N, then Tag as condition (not I64)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(100)), SDDL2_OK);

    size_t skip_count = 0;
    EXPECT_EQ(SDDL2_op_jump_if(stack_, &skip_count), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2JumpIfTest, JumpIfTypeMismatchN)
{
    // Push Tag as N (not I64), then condition
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);

    size_t skip_count = 0;
    EXPECT_EQ(SDDL2_op_jump_if(stack_, &skip_count), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2JumpIfTest, JumpIfNegativeN)
{
    // Push N=-1 (negative), then condition
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);

    size_t skip_count = 0;
    EXPECT_EQ(SDDL2_op_jump_if(stack_, &skip_count), SDDL2_TYPE_MISMATCH);
}
