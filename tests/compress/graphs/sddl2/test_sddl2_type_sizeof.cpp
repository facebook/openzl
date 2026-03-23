// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * Unit tests for SDDL2 type.sizeof operation.
 *
 * Tests that type.sizeof correctly computes the byte size for:
 * - Primitive types (U8, I32LE, F64LE, etc.)
 * - Array types (type with width > 1)
 * - Structure types
 * - Integration with type.fixed_array and type.structure
 */

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2TypeSizeofTest : public SDDL2StackTest {};
} // namespace

// ============================================================================
// Success Scenarios
// ============================================================================

TEST_F(SDDL2TypeSizeofTest, PrimitiveI32LE)
{
    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type({ .kind = SDDL2_TYPE_I32LE, .width = 1 })),
            SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_sizeof(stack_), SDDL2_OK);

    EXPECT_EQ(SDDL2_Stack_depth(stack_), 1);
    popAndVerifyI64(stack_, 4);
}

TEST_F(SDDL2TypeSizeofTest, VariousPrimitives)
{
    struct {
        SDDL2_Type_kind kind;
        int64_t expected_size;
    } test_cases[] = {
        { SDDL2_TYPE_U8, 1 },    { SDDL2_TYPE_I8, 1 },
        { SDDL2_TYPE_BYTES, 1 }, { SDDL2_TYPE_U16LE, 2 },
        { SDDL2_TYPE_I16BE, 2 }, { SDDL2_TYPE_F16LE, 2 },
        { SDDL2_TYPE_U32LE, 4 }, { SDDL2_TYPE_I32BE, 4 },
        { SDDL2_TYPE_F32LE, 4 }, { SDDL2_TYPE_U64LE, 8 },
        { SDDL2_TYPE_I64BE, 8 }, { SDDL2_TYPE_F64LE, 8 },
    };

    for (const auto& tc : test_cases) {
        SDDL2_Stack_init(stack_);

        ASSERT_EQ(
                SDDL2_Stack_push(
                        stack_,
                        SDDL2_Value_type({ .kind = tc.kind, .width = 1 })),
                SDDL2_OK);

        ASSERT_EQ(SDDL2_op_type_sizeof(stack_), SDDL2_OK);

        SDDL2_Value result;
        ASSERT_EQ(SDDL2_Stack_pop(stack_, &result), SDDL2_OK);
        EXPECT_EQ(result.kind, SDDL2_VALUE_I64);
        EXPECT_EQ(result.value.as_i64, tc.expected_size) << "kind=" << tc.kind;
    }
}

TEST_F(SDDL2TypeSizeofTest, ArrayType)
{
    // I16LE with width=10 -> 2 * 10 = 20 bytes
    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type(
                            { .kind = SDDL2_TYPE_I16LE, .width = 10 })),
            SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_sizeof(stack_), SDDL2_OK);

    popAndVerifyI64(stack_, 20);
}

TEST_F(SDDL2TypeSizeofTest, WithFixedArray)
{
    // Create U32LE[5] via type.fixed_array, then sizeof -> 4 * 5 = 20
    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type({ .kind = SDDL2_TYPE_U32LE, .width = 1 })),
            SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_type_fixed_array(stack_), SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_sizeof(stack_), SDDL2_OK);

    popAndVerifyI64(stack_, 20);
}

TEST_F(SDDL2TypeSizeofTest, StructureType)
{
    // Build structure: {U8, I16LE, I32LE} -> 1 + 2 + 4 = 7
    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type(
                            SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                        .width       = 1,
                                        .struct_data = nullptr })),
            SDDL2_OK);
    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type(
                            SDDL2_Type{ .kind        = SDDL2_TYPE_I16LE,
                                        .width       = 1,
                                        .struct_data = nullptr })),
            SDDL2_OK);
    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type(
                            SDDL2_Type{ .kind        = SDDL2_TYPE_I32LE,
                                        .width       = 1,
                                        .struct_data = nullptr })),
            SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_structure(stack_, alloc_fn, alloc_ctx_), SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_sizeof(stack_), SDDL2_OK);

    popAndVerifyI64(stack_, 7);
}

TEST_F(SDDL2TypeSizeofTest, StructureWithArrayMembers)
{
    // Build structure: {U8, I16LE[5], I32LE} -> 1 + (2*5) + 4 = 15
    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type(
                            SDDL2_Type{ .kind        = SDDL2_TYPE_U8,
                                        .width       = 1,
                                        .struct_data = nullptr })),
            SDDL2_OK);
    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type(
                            SDDL2_Type{ .kind        = SDDL2_TYPE_I16LE,
                                        .width       = 5,
                                        .struct_data = nullptr })),
            SDDL2_OK);
    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type(
                            SDDL2_Type{ .kind        = SDDL2_TYPE_I32LE,
                                        .width       = 1,
                                        .struct_data = nullptr })),
            SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_structure(stack_, alloc_fn, alloc_ctx_), SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_sizeof(stack_), SDDL2_OK);

    popAndVerifyI64(stack_, 15);
}

TEST_F(SDDL2TypeSizeofTest, LargeArray)
{
    // U64LE[1000] = 8 * 1000 = 8000
    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type(
                            { .kind = SDDL2_TYPE_U64LE, .width = 1000 })),
            SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_sizeof(stack_), SDDL2_OK);

    popAndVerifyI64(stack_, 8000);
}

TEST_F(SDDL2TypeSizeofTest, BytesArray)
{
    // BYTES[100] = 1 * 100 = 100
    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type(
                            { .kind = SDDL2_TYPE_BYTES, .width = 100 })),
            SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_sizeof(stack_), SDDL2_OK);

    popAndVerifyI64(stack_, 100);
}

// ============================================================================
// Error Scenarios
// ============================================================================

TEST_F(SDDL2TypeSizeofTest, ErrorEmptyStack)
{
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);

    EXPECT_EQ(SDDL2_op_type_sizeof(stack_), SDDL2_STACK_UNDERFLOW);

    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);
}

TEST_F(SDDL2TypeSizeofTest, ErrorTopIsI64)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(42)), SDDL2_OK);

    EXPECT_EQ(SDDL2_op_type_sizeof(stack_), SDDL2_TYPE_MISMATCH);

    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);
}

TEST_F(SDDL2TypeSizeofTest, ErrorTopIsTag)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(100)), SDDL2_OK);

    EXPECT_EQ(SDDL2_op_type_sizeof(stack_), SDDL2_TYPE_MISMATCH);

    EXPECT_EQ(SDDL2_Stack_depth(stack_), 0);
}

TEST_F(SDDL2TypeSizeofTest, ErrorMultipleValuesTopWrong)
{
    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type({ .kind = SDDL2_TYPE_U32LE, .width = 1 })),
            SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(99)), SDDL2_OK);

    EXPECT_EQ(SDDL2_Stack_depth(stack_), 2);

    EXPECT_EQ(SDDL2_op_type_sizeof(stack_), SDDL2_TYPE_MISMATCH);

    // Type value remains on stack
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 1);
}

// ============================================================================
// Integration Scenarios
// ============================================================================

TEST_F(SDDL2TypeSizeofTest, SizeofForValidation)
{
    // Build structure: {F64LE, F64LE, BYTES[2]} -> 8 + 8 + 2 = 18
    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type(
                            SDDL2_Type{ .kind        = SDDL2_TYPE_F64LE,
                                        .width       = 1,
                                        .struct_data = nullptr })),
            SDDL2_OK);
    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type(
                            SDDL2_Type{ .kind        = SDDL2_TYPE_F64LE,
                                        .width       = 1,
                                        .struct_data = nullptr })),
            SDDL2_OK);
    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type(
                            SDDL2_Type{ .kind        = SDDL2_TYPE_BYTES,
                                        .width       = 2,
                                        .struct_data = nullptr })),
            SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_structure(stack_, alloc_fn, alloc_ctx_), SDDL2_OK);

    // Duplicate the type for validation
    ASSERT_EQ(SDDL2_op_dup(stack_, nullptr, 0), SDDL2_OK);

    ASSERT_EQ(SDDL2_op_type_sizeof(stack_), SDDL2_OK);

    popAndVerifyI64(stack_, 18);

    // Original type still on stack
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 1);
}

TEST_F(SDDL2TypeSizeofTest, PreservesStackBelow)
{
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(100)), SDDL2_OK);
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(200)), SDDL2_OK);

    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type({ .kind = SDDL2_TYPE_I32LE, .width = 1 })),
            SDDL2_OK);

    ASSERT_EQ(SDDL2_Stack_depth(stack_), 3);

    ASSERT_EQ(SDDL2_op_type_sizeof(stack_), SDDL2_OK);

    // Should have 3 elements: I64(100), I64(200), I64(4)
    EXPECT_EQ(SDDL2_Stack_depth(stack_), 3);

    popAndVerifyI64(stack_, 4);
    popAndVerifyI64(stack_, 200);
    popAndVerifyI64(stack_, 100);
}

TEST_F(SDDL2TypeSizeofTest, MultipleTimes)
{
    ASSERT_EQ(
            SDDL2_Stack_push(
                    stack_,
                    SDDL2_Value_type({ .kind = SDDL2_TYPE_U64LE, .width = 5 })),
            SDDL2_OK);

    // Duplicate it
    ASSERT_EQ(SDDL2_op_dup(stack_, nullptr, 0), SDDL2_OK);

    // Get sizeof first copy
    ASSERT_EQ(SDDL2_op_type_sizeof(stack_), SDDL2_OK);
    popAndVerifyI64(stack_, 40); // 8 * 5

    // Get sizeof second copy
    ASSERT_EQ(SDDL2_op_type_sizeof(stack_), SDDL2_OK);
    popAndVerifyI64(stack_, 40);
}
