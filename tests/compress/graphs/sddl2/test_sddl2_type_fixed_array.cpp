// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <climits>

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2TypeFixedArrayTest : public SDDL2StackTest {};
} // namespace

// ============================================================================
// Success Scenarios
// ============================================================================

TEST_F(SDDL2TypeFixedArrayTest, BasicArrayType)
{
    SDDL2_Type base_type = { .kind = SDDL2_TYPE_U32LE, .width = 1 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(base_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_fixed_array(stack_), SDDL2_OK);

    EXPECT_EQ(SDDL2_Stack_depth(stack_), 1);

    SDDL2_Value result;
    ASSERT_EQ(SDDL2_Stack_pop(stack_, &result), SDDL2_OK);
    EXPECT_EQ(result.kind, SDDL2_VALUE_TYPE);
    EXPECT_EQ(result.value.as_type.kind, SDDL2_TYPE_U32LE);
    EXPECT_EQ(result.value.as_type.width, 10);
}

TEST_F(SDDL2TypeFixedArrayTest, NestedArrays)
{
    // I16LE with width=1
    SDDL2_Type base_type = { .kind = SDDL2_TYPE_I16LE, .width = 1 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(base_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);

    // Create I16LE[5]
    ASSERT_EQ(SDDL2_op_type_fixed_array(stack_), SDDL2_OK);

    // Create array of 3 of those: I16LE[15]
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_type_fixed_array(stack_), SDDL2_OK);

    SDDL2_Value result;
    ASSERT_EQ(SDDL2_Stack_pop(stack_, &result), SDDL2_OK);
    EXPECT_EQ(result.kind, SDDL2_VALUE_TYPE);
    EXPECT_EQ(result.value.as_type.kind, SDDL2_TYPE_I16LE);
    EXPECT_EQ(result.value.as_type.width, 15); // 5 * 3
}

TEST_F(SDDL2TypeFixedArrayTest, ArrayCountOne)
{
    // F32BE with width=7
    SDDL2_Type base_type = { .kind = SDDL2_TYPE_F32BE, .width = 7 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(base_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_fixed_array(stack_), SDDL2_OK);

    SDDL2_Value result;
    ASSERT_EQ(SDDL2_Stack_pop(stack_, &result), SDDL2_OK);
    EXPECT_EQ(result.kind, SDDL2_VALUE_TYPE);
    EXPECT_EQ(result.value.as_type.kind, SDDL2_TYPE_F32BE);
    EXPECT_EQ(result.value.as_type.width, 7); // 7 * 1
}

TEST_F(SDDL2TypeFixedArrayTest, ZeroWidthArray)
{
    SDDL2_Type base_type = { .kind = SDDL2_TYPE_U8, .width = 1 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(base_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_fixed_array(stack_), SDDL2_OK);

    SDDL2_Value result;
    ASSERT_EQ(SDDL2_Stack_pop(stack_, &result), SDDL2_OK);
    EXPECT_EQ(result.kind, SDDL2_VALUE_TYPE);
    EXPECT_EQ(result.value.as_type.kind, SDDL2_TYPE_U8);
    EXPECT_EQ(result.value.as_type.width, 0);
}

TEST_F(SDDL2TypeFixedArrayTest, MaxSafeValue)
{
    SDDL2_Type base_type = { .kind = SDDL2_TYPE_I8, .width = 1 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(base_type)), SDDL2_OK);
    ASSERT_EQ(
            SDDL2_Stack_push(stack_, SDDL2_Value_i64((int64_t)UINT32_MAX)),
            SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_fixed_array(stack_), SDDL2_OK);

    SDDL2_Value result;
    ASSERT_EQ(SDDL2_Stack_pop(stack_, &result), SDDL2_OK);
    EXPECT_EQ(result.value.as_type.width, UINT32_MAX);
}

TEST_F(SDDL2TypeFixedArrayTest, AllTypeKinds)
{
    SDDL2_Type_kind types[] = {
        SDDL2_TYPE_BYTES, SDDL2_TYPE_U8,    SDDL2_TYPE_I16LE,  SDDL2_TYPE_U32BE,
        SDDL2_TYPE_I64LE, SDDL2_TYPE_F32LE, SDDL2_TYPE_BF16BE,
    };

    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        SDDL2_Stack_init(stack_);

        SDDL2_Type base_type = { .kind = types[i], .width = 2 };
        ASSERT_EQ(
                SDDL2_Stack_push(stack_, SDDL2_Value_type(base_type)),
                SDDL2_OK);
        ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);

        ASSERT_EQ(SDDL2_op_type_fixed_array(stack_), SDDL2_OK);

        SDDL2_Value result;
        ASSERT_EQ(SDDL2_Stack_pop(stack_, &result), SDDL2_OK);
        EXPECT_EQ(result.value.as_type.kind, types[i]);
        EXPECT_EQ(result.value.as_type.width, 10); // 2 * 5
    }
}

// ============================================================================
// Error Scenarios
// ============================================================================

TEST_F(SDDL2TypeFixedArrayTest, ErrorWidthOverflow)
{
    SDDL2_Type base_type = { .kind  = SDDL2_TYPE_U32LE,
                             .width = UINT32_MAX / 2 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(base_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);

    EXPECT_EQ(SDDL2_op_type_fixed_array(stack_), SDDL2_MATH_OVERFLOW);

    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);
}

TEST_F(SDDL2TypeFixedArrayTest, ErrorWidthOverflowBoundary)
{
    SDDL2_Type base_type = { .kind = SDDL2_TYPE_U8, .width = UINT32_MAX };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(base_type)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);

    EXPECT_EQ(SDDL2_op_type_fixed_array(stack_), SDDL2_MATH_OVERFLOW);
}

TEST_F(SDDL2TypeFixedArrayTest, ErrorWrongType)
{
    // Push I64 instead of Type
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);

    EXPECT_EQ(SDDL2_op_type_fixed_array(stack_), SDDL2_TYPE_MISMATCH);

    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);
}

TEST_F(SDDL2TypeFixedArrayTest, ErrorStackUnderflow)
{
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);

    EXPECT_EQ(SDDL2_op_type_fixed_array(stack_), SDDL2_STACK_UNDERFLOW);
}
