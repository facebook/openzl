// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {

class SDDL2VarTest : public SDDL2StackTest {
   protected:
    void SetUp() override
    {
        SDDL2StackTest::SetUp();
        SDDL2_Var_registers_init(&regs_);
    }

    SDDL2_Var_registers regs_{};
};

} // namespace

// ============================================================================
// var.store Tests
// ============================================================================

TEST_F(SDDL2VarTest, StoreI64)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_var_store(stack_, NULL, 0, &regs_), SDDL2_OK);
    EXPECT_EQ(regs_.occupied[0], 1);
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0u);
}

TEST_F(SDDL2VarTest, StoreTag)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(100)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_var_store(stack_, NULL, 0, &regs_), SDDL2_OK);
    EXPECT_EQ(regs_.occupied[5], 1);
}

TEST_F(SDDL2VarTest, StoreType)
{
    SDDL2_Type t = { .kind = SDDL2_TYPE_I32LE, .width = 1 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(t)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_var_store(stack_, NULL, 0, &regs_), SDDL2_OK);
    EXPECT_EQ(regs_.occupied[10], 1);
}

TEST_F(SDDL2VarTest, StoreOverwrite)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_var_store(stack_, NULL, 0, &regs_), SDDL2_OK);

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(99)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_var_store(stack_, NULL, 0, &regs_), SDDL2_OK);

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_var_load(stack_, NULL, 0, &regs_), SDDL2_OK);
    popAndVerifyI64(stack_, 99);
}

TEST_F(SDDL2VarTest, StoreBoundsNegative)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_var_store(stack_, NULL, 0, &regs_), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2VarTest, StoreBoundsOverflow)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(256)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_var_store(stack_, NULL, 0, &regs_), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2VarTest, StoreTypeMismatch)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(0)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_var_store(stack_, NULL, 0, &regs_), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2VarTest, StoreUnderflowEmpty)
{
    EXPECT_EQ(
            SDDL2_op_var_store(stack_, NULL, 0, &regs_), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2VarTest, StoreUnderflowOneValue)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    EXPECT_EQ(
            SDDL2_op_var_store(stack_, NULL, 0, &regs_), SDDL2_STACK_UNDERFLOW);
}

// ============================================================================
// var.load Tests
// ============================================================================

TEST_F(SDDL2VarTest, LoadI64)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_var_store(stack_, NULL, 0, &regs_), SDDL2_OK);

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_var_load(stack_, NULL, 0, &regs_), SDDL2_OK);
    popAndVerifyI64(stack_, 42);
}

TEST_F(SDDL2VarTest, LoadTag)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(100)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_var_store(stack_, NULL, 0, &regs_), SDDL2_OK);

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_var_load(stack_, NULL, 0, &regs_), SDDL2_OK);
    SDDL2_Value result;
    ASSERT_EQ(SDDL2_Stack_pop(stack_, &result), SDDL2_OK);
    EXPECT_EQ(result.kind, SDDL2_VALUE_TAG);
    EXPECT_EQ(result.value.as_tag, 100u);
}

TEST_F(SDDL2VarTest, LoadType)
{
    SDDL2_Type t = { .kind = SDDL2_TYPE_I32LE, .width = 1 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(t)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_var_store(stack_, NULL, 0, &regs_), SDDL2_OK);

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(10)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_var_load(stack_, NULL, 0, &regs_), SDDL2_OK);
    SDDL2_Value result;
    ASSERT_EQ(SDDL2_Stack_pop(stack_, &result), SDDL2_OK);
    EXPECT_EQ(result.kind, SDDL2_VALUE_TYPE);
    EXPECT_EQ(result.value.as_type.kind, SDDL2_TYPE_I32LE);
    EXPECT_EQ(result.value.as_type.width, 1u);
}

TEST_F(SDDL2VarTest, LoadUninitialized)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_var_load(stack_, NULL, 0, &regs_), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2VarTest, LoadBoundsNegative)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_var_load(stack_, NULL, 0, &regs_), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2VarTest, LoadBoundsOverflow)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(256)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_var_load(stack_, NULL, 0, &regs_), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2VarTest, LoadTypeMismatch)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(0)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_var_load(stack_, NULL, 0, &regs_), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2VarTest, LoadUnderflow)
{
    EXPECT_EQ(
            SDDL2_op_var_load(stack_, NULL, 0, &regs_), SDDL2_STACK_UNDERFLOW);
}

// ============================================================================
// Combined / Integration Tests
// ============================================================================

TEST_F(SDDL2VarTest, StoreLoadRoundTrip)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(12345)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_var_store(stack_, NULL, 0, &regs_), SDDL2_OK);

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_var_load(stack_, NULL, 0, &regs_), SDDL2_OK);
    popAndVerifyI64(stack_, 12345);
}

TEST_F(SDDL2VarTest, MultipleRegisters)
{
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(100 + i)), SDDL2_OK);
        ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(i)), SDDL2_OK);
        ASSERT_EQ(SDDL2_op_var_store(stack_, NULL, 0, &regs_), SDDL2_OK);
    }

    for (int i = 2; i >= 0; i--) {
        ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(i)), SDDL2_OK);
        ASSERT_EQ(SDDL2_op_var_load(stack_, NULL, 0, &regs_), SDDL2_OK);
        popAndVerifyI64(stack_, 100 + i);
    }
}
