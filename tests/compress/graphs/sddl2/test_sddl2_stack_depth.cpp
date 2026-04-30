// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * Comprehensive tests for SDDL2 push.stack_depth operation
 * Tests the stack introspection opcode
 */

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2StackDepthTest : public SDDL2StackTest {};
class SDDL2StackDepthSmallTest : public SDDL2StackTestCustomCapacity<10> {};
class SDDL2StackDepthLargeTest : public SDDL2StackTestCustomCapacity<1000> {};
} // namespace

// ============================================================================
// push.stack_depth Basic Tests
// ============================================================================

TEST_F(SDDL2StackDepthTest, PushStackDepthEmptyStack)
{
    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 1u);
    popAndVerifyI64(stack_, 0);
}

TEST_F(SDDL2StackDepthTest, PushStackDepthOneElement)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 2u);
    popAndVerifyI64(stack_, 1);
}

TEST_F(SDDL2StackDepthTest, PushStackDepthMultipleElements)
{
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(i)), SDDL2_OK);
    }
    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 6u);
    popAndVerifyI64(stack_, 5);
}

TEST_F(SDDL2StackDepthLargeTest, PushStackDepthLargeDepth)
{
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(i)), SDDL2_OK);
    }
    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 101u);
    popAndVerifyI64(stack_, 100);
}

// ============================================================================
// push.stack_depth Interaction Tests
// ============================================================================

TEST_F(SDDL2StackDepthTest, PushStackDepthAfterPush)
{
    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    popAndVerifyI64(stack_, 0);

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    popAndVerifyI64(stack_, 1);

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(20)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(30)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    popAndVerifyI64(stack_, 3);
}

TEST_F(SDDL2StackDepthTest, PushStackDepthAfterPop)
{
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(i)), SDDL2_OK);
    }

    SDDL2_Value val;
    ASSERT_EQ(SDDL2_Stack_pop(stack_, &val), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_pop(stack_, &val), SDDL2_OK);

    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    popAndVerifyI64(stack_, 3);
}

TEST_F(SDDL2StackDepthTest, PushStackDepthWithDifferentTypes)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(100)), SDDL2_OK);
    SDDL2_Type type = { .kind = SDDL2_TYPE_U32LE, .width = 1 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);

    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 4u);
    popAndVerifyI64(stack_, 3);
}

// ============================================================================
// push.stack_depth Overflow Test
// ============================================================================

TEST_F(SDDL2StackDepthSmallTest, PushStackDepthNearCapacity)
{
    for (int i = 0; i < 9; i++) {
        ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(i)), SDDL2_OK);
    }

    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 10u);
    popAndVerifyI64(stack_, 9);
}

TEST_F(SDDL2StackDepthSmallTest, PushStackDepthOverflow)
{
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(i)), SDDL2_OK);
    }

    EXPECT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_STACK_OVERFLOW);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 10u);
}

// ============================================================================
// push.stack_depth Combined Operations Tests
// ============================================================================

TEST_F(SDDL2StackDepthTest, PushStackDepthWithArithmetic)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(20)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(30)), SDDL2_OK);

    // Get depth (3) and multiply by 10
    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_mul(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 30); // 3 * 10
}

TEST_F(SDDL2StackDepthTest, PushStackDepthWithComparison)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);

    // Check if depth equals 2
    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_eq(stack_, NULL, 0), SDDL2_OK);
    popAndVerifyI64(stack_, 1); // True
}

TEST_F(SDDL2StackDepthTest, PushStackDepthValidationPattern)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(20)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(30)), SDDL2_OK);

    // Validate depth == 3
    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_eq(stack_, NULL, 0), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_expect_true(stack_, NULL), SDDL2_OK);
}

TEST_F(SDDL2StackDepthTest, PushStackDepthMultipleCalls)
{
    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    popAndVerifyI64(stack_, 0);

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    // After operations: [1, depth(1)=1, depth(2)=2] = 3 elements
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 3u);
    popAndVerifyI64(stack_, 2);
    popAndVerifyI64(stack_, 1);
}

// ============================================================================
// push.stack_depth Edge Cases
// ============================================================================

TEST_F(SDDL2StackDepthTest, PushStackDepthAfterDup)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_dup(stack_, NULL, 0), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    popAndVerifyI64(stack_, 2);
}

TEST_F(SDDL2StackDepthTest, PushStackDepthAfterSwap)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_swap(stack_, NULL, 0), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    popAndVerifyI64(stack_, 2);
}

TEST_F(SDDL2StackDepthTest, PushStackDepthAfterDrop)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_drop(stack_, NULL, 0), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_push_stack_depth(stack_), SDDL2_OK);
    popAndVerifyI64(stack_, 2);
}
