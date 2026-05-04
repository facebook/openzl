// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * Basic unit tests for OpenZL VM stack operations.
 * Tests Phase 1: Foundation (Stack + Values)
 */

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2VmTest : public SDDL2StackTest {};
class SDDL2VmSmallStackTest : public SDDL2StackTestCustomCapacity<10> {};
} // namespace

// ============================================================================
// Stack Operations Tests
// ============================================================================

TEST_F(SDDL2VmTest, StackInit)
{
    EXPECT_NE(stack_, nullptr);
    EXPECT_EQ(stack_->capacity, 100u);
}

TEST_F(SDDL2VmTest, StackPushPop)
{
    // Push an I64 value
    SDDL2_Value v1 = SDDL2_Value_i64(42);
    ASSERT_EQ(SDDL2_Stack_push(stack_, v1), SDDL2_OK);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 1u);
    EXPECT_FALSE(SDDL2_Stack_is_empty(stack_));

    // Push a Tag value
    SDDL2_Value v2 = SDDL2_Value_tag(100);
    ASSERT_EQ(SDDL2_Stack_push(stack_, v2), SDDL2_OK);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 2u);

    // Pop and verify Tag
    SDDL2_Value popped;
    ASSERT_EQ(popValue(stack_, &popped), SDDL2_OK);
    EXPECT_EQ(popped.kind, SDDL2_VALUE_TAG);
    EXPECT_EQ(popped.value.as_tag, 100u);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 1u);

    // Pop and verify I64
    ASSERT_EQ(popValue(stack_, &popped), SDDL2_OK);
    EXPECT_EQ(popped.kind, SDDL2_VALUE_I64);
    EXPECT_EQ(popped.value.as_i64, 42);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0u);
}

TEST_F(SDDL2VmTest, StackUnderflow)
{
    SDDL2_Value v;
    EXPECT_EQ(popValue(stack_, &v), SDDL2_STACK_UNDERFLOW);
    EXPECT_EQ(SDDL2_Stack_peek(stack_, &v), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2VmSmallStackTest, StackOverflow)
{
    // Fill stack to max capacity (10)
    SDDL2_Value v = SDDL2_Value_i64(1);
    for (size_t i = 0; i < 10; i++) {
        ASSERT_EQ(SDDL2_Stack_push(stack_, v), SDDL2_OK);
    }

    // Try to push one more - should overflow
    EXPECT_EQ(SDDL2_Stack_push(stack_, v), SDDL2_STACK_OVERFLOW);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 10u);
}

TEST_F(SDDL2VmTest, StackPeek)
{
    SDDL2_Value v1 = SDDL2_Value_i64(123);
    ASSERT_EQ(SDDL2_Stack_push(stack_, v1), SDDL2_OK);

    // Peek should not modify stack
    SDDL2_Value peeked;
    ASSERT_EQ(SDDL2_Stack_peek(stack_, &peeked), SDDL2_OK);
    EXPECT_EQ(peeked.kind, SDDL2_VALUE_I64);
    EXPECT_EQ(peeked.value.as_i64, 123);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 1u);

    // Peek again - should return same value
    ASSERT_EQ(SDDL2_Stack_peek(stack_, &peeked), SDDL2_OK);
    EXPECT_EQ(peeked.value.as_i64, 123);
}

// ============================================================================
// Value Kind Tests
// ============================================================================

TEST_F(SDDL2VmTest, ValueKinds)
{
    // Test I64 value
    SDDL2_Value v_i64 = SDDL2_Value_i64(-9223372036854775807LL);
    EXPECT_EQ(v_i64.kind, SDDL2_VALUE_I64);
    EXPECT_EQ(v_i64.value.as_i64, -9223372036854775807LL);

    // Test Tag value
    SDDL2_Value v_tag = SDDL2_Value_tag(0xDEADBEEF);
    EXPECT_EQ(v_tag.kind, SDDL2_VALUE_TAG);
    EXPECT_EQ(v_tag.value.as_tag, 0xDEADBEEFu);

    // Test Type value
    SDDL2_Type t       = { .kind = SDDL2_TYPE_I32LE, .width = 4 };
    SDDL2_Value v_type = SDDL2_Value_type(t);
    EXPECT_EQ(v_type.kind, SDDL2_VALUE_TYPE);
    EXPECT_EQ(v_type.value.as_type.kind, SDDL2_TYPE_I32LE);
    EXPECT_EQ(v_type.value.as_type.width, 4u);
}

// ============================================================================
// Type Size Tests
// ============================================================================

TEST_F(SDDL2VmTest, TypeSizes)
{
    EXPECT_EQ(getKindSize(SDDL2_TYPE_U8), 1u);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_I8), 1u);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_U16LE), 2u);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_I16BE), 2u);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_U32LE), 4u);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_F32BE), 4u);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_I64LE), 8u);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_F64BE), 8u);
    EXPECT_EQ(getKindSize(SDDL2_TYPE_BYTES), 1u);
}
