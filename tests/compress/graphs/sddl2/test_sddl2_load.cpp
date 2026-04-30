// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <cstdint>
#include <cstring>

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2LoadTest : public SDDL2StackTest {};

} // namespace

// ============================================================================
// 8-bit Load Tests
// ============================================================================

TEST_F(SDDL2LoadTest, LoadU8Basic)
{
    uint8_t data[] = { 0x42, 0xFF, 0x00, 0x7F };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Load byte at address 0: 0x42 (66)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 0x42);

    // Load byte at address 1: 0xFF (255, zero-extended)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 0xFF);
}

TEST_F(SDDL2LoadTest, LoadI8SignExtension)
{
    uint8_t data[] = { 0x42, 0xFF, 0x80, 0x7F };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Load signed byte at address 0: 0x42 (66, positive)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_i8(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 66);

    // Load signed byte at address 1: 0xFF (-1, sign-extended)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_i8(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, -1);

    // Load signed byte at address 2: 0x80 (-128, sign-extended)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_i8(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, -128);

    // Load signed byte at address 3: 0x7F (127, positive)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_i8(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 127);
}

// ============================================================================
// 16-bit Load Tests
// ============================================================================

TEST_F(SDDL2LoadTest, LoadU16LEBasic)
{
    uint8_t data[] = { 0x34, 0x12, 0xFF, 0xFF }; // 0x1234 LE, 0xFFFF LE
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Load u16le at address 0: 0x1234
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_u16le(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 0x1234);

    // Load u16le at address 2: 0xFFFF (65535, zero-extended)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_u16le(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 0xFFFF);
}

TEST_F(SDDL2LoadTest, LoadU16BEBasic)
{
    uint8_t data[] = { 0x12, 0x34, 0xFF, 0xFF }; // 0x1234 BE, 0xFFFF BE
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Load u16be at address 0: 0x1234
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_u16be(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 0x1234);
}

TEST_F(SDDL2LoadTest, LoadI16LESignExtension)
{
    uint8_t data[] = { 0xFF, 0xFF, 0xFF, 0x7F }; // -1 LE, 32767 LE
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Load i16le at address 0: 0xFFFF (-1, sign-extended)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_i16le(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, -1);

    // Load i16le at address 2: 0x7FFF (32767, positive)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_i16le(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 32767);
}

TEST_F(SDDL2LoadTest, LoadI16BESignExtension)
{
    uint8_t data[] = { 0xFF, 0xFF, 0x7F, 0xFF }; // -1 BE, 32767 BE
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Load i16be at address 0: 0xFFFF (-1, sign-extended)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_i16be(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, -1);
}

// ============================================================================
// 32-bit Load Tests
// ============================================================================

TEST_F(SDDL2LoadTest, LoadU32LEBasic)
{
    uint8_t data[] = { 0x78, 0x56, 0x34, 0x12 }; // 0x12345678 LE
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Load u32le at address 0: 0x12345678
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_u32le(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 0x12345678);
}

TEST_F(SDDL2LoadTest, LoadU32BEBasic)
{
    uint8_t data[] = { 0x12, 0x34, 0x56, 0x78 }; // 0x12345678 BE
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Load u32be at address 0: 0x12345678
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_u32be(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 0x12345678);
}

TEST_F(SDDL2LoadTest, LoadI32LESignExtension)
{
    uint8_t data[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Load i32le at address 0: 0xFFFFFFFF (-1, sign-extended)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_i32le(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, -1);

    // Load i32le at address 4: 0x7FFFFFFF (2147483647, positive)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(4)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_i32le(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 2147483647);
}

TEST_F(SDDL2LoadTest, LoadI32BESignExtension)
{
    uint8_t data[] = { 0xFF, 0xFF, 0xFF, 0xFF }; // -1 BE
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Load i32be at address 0: 0xFFFFFFFF (-1, sign-extended)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_i32be(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, -1);
}

// ============================================================================
// 64-bit Load Tests
// ============================================================================

TEST_F(SDDL2LoadTest, LoadI64LEBasic)
{
    uint8_t data[] = { 0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Load i64le at address 0: 0x1234567890ABCDEF
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_i64le(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 0x1234567890ABCDEF);
}

TEST_F(SDDL2LoadTest, LoadI64BEBasic)
{
    uint8_t data[] = { 0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Load i64be at address 0: 0x1234567890ABCDEF
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_i64be(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 0x1234567890ABCDEF);
}

// ============================================================================
// Error Condition Tests - Stack Underflow
// ============================================================================

TEST_F(SDDL2LoadTest, LoadU8StackUnderflow)
{
    uint8_t data[] = { 0x42 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    EXPECT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2LoadTest, LoadI8StackUnderflow)
{
    uint8_t data[] = { 0x42 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    EXPECT_EQ(SDDL2_op_load_i8(stack_, &buffer), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2LoadTest, LoadU16LEStackUnderflow)
{
    uint8_t data[] = { 0x42, 0x43 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    EXPECT_EQ(SDDL2_op_load_u16le(stack_, &buffer), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2LoadTest, LoadU16BEStackUnderflow)
{
    uint8_t data[] = { 0x42, 0x43 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    EXPECT_EQ(SDDL2_op_load_u16be(stack_, &buffer), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2LoadTest, LoadI16LEStackUnderflow)
{
    uint8_t data[] = { 0x42, 0x43 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    EXPECT_EQ(SDDL2_op_load_i16le(stack_, &buffer), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2LoadTest, LoadI16BEStackUnderflow)
{
    uint8_t data[] = { 0x42, 0x43 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    EXPECT_EQ(SDDL2_op_load_i16be(stack_, &buffer), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2LoadTest, LoadU32LEStackUnderflow)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    EXPECT_EQ(SDDL2_op_load_u32le(stack_, &buffer), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2LoadTest, LoadU32BEStackUnderflow)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    EXPECT_EQ(SDDL2_op_load_u32be(stack_, &buffer), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2LoadTest, LoadI32LEStackUnderflow)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    EXPECT_EQ(SDDL2_op_load_i32le(stack_, &buffer), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2LoadTest, LoadI32BEStackUnderflow)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    EXPECT_EQ(SDDL2_op_load_i32be(stack_, &buffer), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2LoadTest, LoadI64LEStackUnderflow)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    EXPECT_EQ(SDDL2_op_load_i64le(stack_, &buffer), SDDL2_STACK_UNDERFLOW);
}

TEST_F(SDDL2LoadTest, LoadI64BEStackUnderflow)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    EXPECT_EQ(SDDL2_op_load_i64be(stack_, &buffer), SDDL2_STACK_UNDERFLOW);
}

// ============================================================================
// Error Condition Tests - Type Mismatch
// ============================================================================

TEST_F(SDDL2LoadTest, LoadU8TypeMismatchTag)
{
    uint8_t data[] = { 0x42 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(100)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadU8TypeMismatchType)
{
    uint8_t data[] = { 0x42 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    SDDL2_Type type = { .kind = SDDL2_TYPE_U8, .width = 1 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadI8TypeMismatchTag)
{
    uint8_t data[] = { 0x42 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(200)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i8(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadI8TypeMismatchType)
{
    uint8_t data[] = { 0x42 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    SDDL2_Type type = { .kind = SDDL2_TYPE_I8, .width = 1 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i8(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadU16LETypeMismatchTag)
{
    uint8_t data[] = { 0x42, 0x43 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(300)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u16le(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadU16LETypeMismatchType)
{
    uint8_t data[] = { 0x42, 0x43 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    SDDL2_Type type = { .kind = SDDL2_TYPE_U16LE, .width = 2 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u16le(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadU16BETypeMismatchTag)
{
    uint8_t data[] = { 0x42, 0x43 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(400)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u16be(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadU16BETypeMismatchType)
{
    uint8_t data[] = { 0x42, 0x43 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    SDDL2_Type type = { .kind = SDDL2_TYPE_U16BE, .width = 2 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u16be(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadI16LETypeMismatchTag)
{
    uint8_t data[] = { 0x42, 0x43 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(500)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i16le(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadI16LETypeMismatchType)
{
    uint8_t data[] = { 0x42, 0x43 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    SDDL2_Type type = { .kind = SDDL2_TYPE_I16LE, .width = 2 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i16le(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadI16BETypeMismatchTag)
{
    uint8_t data[] = { 0x42, 0x43 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(600)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i16be(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadI16BETypeMismatchType)
{
    uint8_t data[] = { 0x42, 0x43 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    SDDL2_Type type = { .kind = SDDL2_TYPE_I16BE, .width = 2 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i16be(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadU32LETypeMismatchTag)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(700)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u32le(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadU32LETypeMismatchType)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    SDDL2_Type type = { .kind = SDDL2_TYPE_U32LE, .width = 4 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u32le(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadU32BETypeMismatchTag)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(800)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u32be(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadU32BETypeMismatchType)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    SDDL2_Type type = { .kind = SDDL2_TYPE_U32BE, .width = 4 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u32be(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadI32LETypeMismatchTag)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(900)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i32le(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadI32LETypeMismatchType)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    SDDL2_Type type = { .kind = SDDL2_TYPE_I32LE, .width = 4 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i32le(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadI32BETypeMismatchTag)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(1000)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i32be(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadI32BETypeMismatchType)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    SDDL2_Type type = { .kind = SDDL2_TYPE_I32BE, .width = 4 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i32be(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadI64LETypeMismatchTag)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(1100)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i64le(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadI64LETypeMismatchType)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    SDDL2_Type type = { .kind = SDDL2_TYPE_I64LE, .width = 8 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i64le(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadI64BETypeMismatchTag)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(1200)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i64be(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

TEST_F(SDDL2LoadTest, LoadI64BETypeMismatchType)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    SDDL2_Type type = { .kind = SDDL2_TYPE_I64BE, .width = 8 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(type)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i64be(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

// ============================================================================
// Error Condition Tests - Bounds Checking
// ============================================================================

TEST_F(SDDL2LoadTest, LoadU8BoundsNegativeAddress)
{
    uint8_t data[] = { 0x42 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2LoadTest, LoadU8BoundsBeyondBuffer)
{
    uint8_t data[] = { 0x42 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2LoadTest, LoadI8BoundsBeyondBuffer)
{
    uint8_t data[] = { 0x42 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i8(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2LoadTest, LoadU16LEBoundsBeyondBuffer)
{
    uint8_t data[] = { 0x42, 0x43 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Try to load at address 1 (needs 2 bytes, but only 1 byte left)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u16le(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2LoadTest, LoadU16BEBoundsBeyondBuffer)
{
    uint8_t data[] = { 0x42, 0x43 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u16be(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2LoadTest, LoadI16LEBoundsBeyondBuffer)
{
    uint8_t data[] = { 0x42, 0x43 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i16le(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2LoadTest, LoadI16BEBoundsBeyondBuffer)
{
    uint8_t data[] = { 0x42, 0x43 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i16be(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2LoadTest, LoadU32LEBoundsBeyondBuffer)
{
    uint8_t data[] = { 0x01, 0x02, 0x03 }; // Only 3 bytes
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Try to load u32le at address 0 (needs 4 bytes, but only have 3)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u32le(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2LoadTest, LoadU32BEBoundsBeyondBuffer)
{
    uint8_t data[] = { 0x01, 0x02, 0x03 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u32be(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2LoadTest, LoadI32LEBoundsBeyondBuffer)
{
    uint8_t data[] = { 0x01, 0x02, 0x03 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i32le(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2LoadTest, LoadI32BEBoundsBeyondBuffer)
{
    uint8_t data[] = { 0x01, 0x02, 0x03 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i32be(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2LoadTest, LoadI64LEBoundsBeyondBuffer)
{
    uint8_t data[] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
    }; // Only 7 bytes
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    // Try to load i64le at address 0 (needs 8 bytes, but only have 7)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i64le(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2LoadTest, LoadI64BEBoundsBeyondBuffer)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, data, sizeof(data));

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_i64be(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}
