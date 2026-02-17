// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * Unit tests for stack.drop_if operation.
 *
 * Tests the conditional stack drop operation which:
 * - Pops a condition (I64)
 * - If condition is non-zero, pops and discards the top value
 * - If condition is zero, leaves the top value on the stack
 */

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2StackDropIfTest : public SDDL2StackTest {};
} // namespace

// ============================================================================
// Basic Functionality Tests
// ============================================================================

TEST_F(SDDL2StackDropIfTest, DropIfTrueBasic)
{
    // Push value then condition=1 (true)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);

    // Execute drop_if - should drop the 42
    ASSERT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_OK);

    // Stack should be empty
    EXPECT_EQ(stack_->top, 0u);
}

TEST_F(SDDL2StackDropIfTest, DropIfFalseBasic)
{
    // Push value then condition=0 (false)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);

    // Execute drop_if - should NOT drop the 42
    ASSERT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_OK);

    // Stack should contain the 42
    EXPECT_EQ(stack_->top, 1u);

    SDDL2_Value result;
    ASSERT_EQ(SDDL2_Stack_pop(stack_, &result), SDDL2_OK);
    EXPECT_EQ(result.kind, SDDL2_VALUE_I64);
    EXPECT_EQ(result.value.as_i64, 42);
}

TEST_F(SDDL2StackDropIfTest, DropIfNonzeroIsTrue)
{
    // Push value then condition=100 (non-zero = true)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(99)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(100)), SDDL2_OK);

    // Execute drop_if - should drop the 99
    ASSERT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_OK);

    EXPECT_EQ(stack_->top, 0u);
}

TEST_F(SDDL2StackDropIfTest, DropIfNegativeIsTrue)
{
    // Push value then condition=-1 (negative = true)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(50)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);

    // Execute drop_if - should drop the 50
    ASSERT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_OK);

    EXPECT_EQ(stack_->top, 0u);
}

// ============================================================================
// Type Interactions
// ============================================================================

TEST_F(SDDL2StackDropIfTest, DropIfWithDifferentValueTypes)
{
    // Test with Tag value (should work - any value type can be dropped)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(123)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_OK);
    EXPECT_EQ(stack_->top, 0u);

    // Test with Type value
    SDDL2_Type type = { .kind = SDDL2_TYPE_U8, .width = 1 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_OK);
    EXPECT_EQ(stack_->top, 0u);
}

TEST_F(SDDL2StackDropIfTest, DropIfPreservesValueType)
{
    // Push Tag value and condition=0 (false)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(999)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);

    // Execute drop_if - should NOT drop
    ASSERT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_OK);

    // Verify Tag value is preserved
    SDDL2_Value result;
    ASSERT_EQ(SDDL2_Stack_pop(stack_, &result), SDDL2_OK);
    EXPECT_EQ(result.kind, SDDL2_VALUE_TAG);
    EXPECT_EQ(result.value.as_tag, 999u);
}

// ============================================================================
// Error Cases
// ============================================================================

TEST_F(SDDL2StackDropIfTest, DropIfUnderflowEmptyStack)
{
    EXPECT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2StackDropIfTest, DropIfUnderflowOnlyCondition)
{
    // Only condition, no value to drop
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);

    // Should fail when trying to drop non-existent value
    EXPECT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2StackDropIfTest, DropIfTypeMismatchCondition)
{
    // Push value then Tag as condition (not I64)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(100)), SDDL2_OK);

    // Should fail - condition must be I64
    EXPECT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_TYPE_MISMATCH);
}

// ============================================================================
// Stack State Tests
// ============================================================================

TEST_F(SDDL2StackDropIfTest, DropIfMultipleValuesOnStack)
{
    // Push several values, then drop_if on top one
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(20)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(30)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);

    // Drop the 30
    ASSERT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_OK);

    // Stack should have 10, 20 remaining
    EXPECT_EQ(stack_->top, 2u);

    SDDL2_Value v1, v2;
    ASSERT_EQ(SDDL2_Stack_pop(stack_, &v1), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_pop(stack_, &v2), SDDL2_OK);
    EXPECT_EQ(v1.value.as_i64, 20);
    EXPECT_EQ(v2.value.as_i64, 10);
}

TEST_F(SDDL2StackDropIfTest, DropIfLeavesStackUnchangedWhenFalse)
{
    // Push several values
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(20)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(30)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);

    // Execute drop_if
    ASSERT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_OK);

    // Stack should still have 10, 20, 30
    EXPECT_EQ(stack_->top, 3u);

    SDDL2_Value v1, v2, v3;
    ASSERT_EQ(SDDL2_Stack_pop(stack_, &v1), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_pop(stack_, &v2), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_pop(stack_, &v3), SDDL2_OK);
    EXPECT_EQ(v1.value.as_i64, 30);
    EXPECT_EQ(v2.value.as_i64, 20);
    EXPECT_EQ(v3.value.as_i64, 10);
}

// ============================================================================
// Sequential Operations
// ============================================================================

TEST_F(SDDL2StackDropIfTest, DropIfSequence)
{
    // Test sequence: drop true, drop false, drop true
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_OK); // drops 1

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_OK); // keeps 2

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_OK); // drops 3

    // Only 2 should remain
    EXPECT_EQ(stack_->top, 1u);
    popAndVerifyI64(stack_, 2);
}

// ============================================================================
// Combined with Other Operations
// ============================================================================

TEST_F(SDDL2StackDropIfTest, DropIfWithComparison)
{
    // Pattern: compare two values, drop result based on comparison
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(999)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_gt(stack_, NULL, 0), SDDL2_OK); // 10 > 5 = 1 (true)

    // Should drop 999
    ASSERT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_OK);
    EXPECT_EQ(stack_->top, 0u);
}

TEST_F(SDDL2StackDropIfTest, DropIfWithArithmetic)
{
    // Use arithmetic result as condition
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_sub(stack_, NULL, 0), SDDL2_OK); // 10 - 10 = 0 (false)

    // Should NOT drop 42
    ASSERT_EQ(SDDL2_op_stack_drop_if(stack_, NULL, 0), SDDL2_OK);
    EXPECT_EQ(stack_->top, 1u);
    popAndVerifyI64(stack_, 42);
}
