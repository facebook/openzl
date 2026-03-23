// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * Unit tests for OpenZL VM input buffer operations.
 * Tests Phase 3: Input Buffer
 */

#include "tests/compress/graphs/sddl2/utils.h"

using namespace openzl::sddl2::testing;

namespace {
class SDDL2InputTest : public SDDL2StackTest {};
} // namespace

// ============================================================================
// Input Buffer Initialization Tests
// ============================================================================

TEST_F(SDDL2InputTest, BufferInit)
{
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 5);

    EXPECT_EQ(buffer.data, data);
    EXPECT_EQ(buffer.size, 5u);
    EXPECT_EQ(buffer.current_pos, 0u);
}

// ============================================================================
// current_pos Operation Tests
// ============================================================================

TEST_F(SDDL2InputTest, CurrentPosAtStart)
{
    uint8_t data[] = { 0xAA, 0xBB, 0xCC };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 3);

    // Get current_pos (should be 0)
    ASSERT_EQ(SDDL2_op_current_pos(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 0);
}

TEST_F(SDDL2InputTest, CurrentPosAfterAdvance)
{
    uint8_t data[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 5);

    // Manually advance cursor (simulating segment creation)
    buffer.current_pos = 3;

    // Get current_pos (should be 3)
    ASSERT_EQ(SDDL2_op_current_pos(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 3);
}

TEST_F(SDDL2InputTest, CurrentPosNoAdvance)
{
    uint8_t data[] = { 0x01, 0x02 };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 2);
    buffer.current_pos = 1;

    // Call current_pos multiple times - should not change cursor
    ASSERT_EQ(SDDL2_op_current_pos(stack_, &buffer), SDDL2_OK);
    EXPECT_EQ(buffer.current_pos, 1u);

    ASSERT_EQ(SDDL2_op_current_pos(stack_, &buffer), SDDL2_OK);
    EXPECT_EQ(buffer.current_pos, 1u);

    // Stack should have two copies of position
    popAndVerifyI64(stack_, 1);
    popAndVerifyI64(stack_, 1);
}

// ============================================================================
// load.u8 Operation Tests
// ============================================================================

TEST_F(SDDL2InputTest, LoadU8Basic)
{
    uint8_t data[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 4);

    // Load byte at address 0 (should be 0xAA)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 0xAA);

    // Load byte at address 2 (should be 0xCC)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 0xCC);
}

TEST_F(SDDL2InputTest, LoadU8AllBytes)
{
    uint8_t data[] = { 0x00, 0xFF, 0x42, 0x7F, 0x80 };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 5);

    // Load all bytes
    for (size_t i = 0; i < 5; i++) {
        ASSERT_EQ(
                SDDL2_Stack_push(stack_, SDDL2_Value_i64((int64_t)i)),
                SDDL2_OK);
        ASSERT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_OK);
        popAndVerifyI64(stack_, (int64_t)data[i]);
    }
}

TEST_F(SDDL2InputTest, LoadU8NoCursorAdvance)
{
    uint8_t data[] = { 0x11, 0x22, 0x33 };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 3);
    size_t original_pos = buffer.current_pos;

    // Load byte - should NOT advance cursor
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_OK);

    EXPECT_EQ(buffer.current_pos, original_pos);
    popAndVerifyI64(stack_, 0x22);
}

// ============================================================================
// Bounds Checking Tests
// ============================================================================

TEST_F(SDDL2InputTest, LoadU8OutOfBoundsPositive)
{
    uint8_t data[] = { 0x01, 0x02, 0x03 };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 3);

    // Try to load at address 3 (out of bounds, valid range is 0-2)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(3)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_LOAD_BOUNDS);

    // Try to load at address 100 (way out of bounds)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(100)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2InputTest, LoadU8OutOfBoundsNegative)
{
    uint8_t data[] = { 0xAA, 0xBB };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 2);

    // Try to load at negative address
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-1)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_LOAD_BOUNDS);

    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(-100)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2InputTest, LoadU8AtLastValidAddress)
{
    uint8_t data[] = { 0x10, 0x20, 0x30, 0x40, 0x50 };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 5);

    // Load at last valid address (4)
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(4)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 0x50);

    // Load at address 5 should fail
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(5)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}

TEST_F(SDDL2InputTest, LoadU8EmptyBuffer)
{
    uint8_t* data = NULL;
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 0);

    // Any address should fail on empty buffer
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_LOAD_BOUNDS);
}

// ============================================================================
// Type Mismatch Tests
// ============================================================================

TEST_F(SDDL2InputTest, LoadU8TypeMismatch)
{
    uint8_t data[] = { 0xAA, 0xBB };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 2);

    // Try to load with Tag instead of I64
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_tag(42)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_TYPE_MISMATCH);

    // Try to load with Type instead of I64
    SDDL2_Type t = { .kind = SDDL2_TYPE_U8, .width = 1 };
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_type(t)), SDDL2_OK);
    EXPECT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_TYPE_MISMATCH);
}

// ============================================================================
// Stack Underflow Tests
// ============================================================================

TEST_F(SDDL2InputTest, LoadU8Underflow)
{
    uint8_t data[] = { 0xAA };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 1);

    // Try to load with empty stack
    EXPECT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_STACK_UNDERFLOW);
}

// ============================================================================
// Combined Operation Tests
// ============================================================================

TEST_F(SDDL2InputTest, CurrentPosAndLoad)
{
    uint8_t data[] = { 0x10, 0x20, 0x30, 0x40 };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 4);
    buffer.current_pos = 2; // Simulate being midway through

    // Get current position
    ASSERT_EQ(SDDL2_op_current_pos(stack_, &buffer), SDDL2_OK);

    // Use it to load the byte at current position
    ASSERT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 0x30); // data[2]

    // Position should still be 2
    EXPECT_EQ(buffer.current_pos, 2u);
}

TEST_F(SDDL2InputTest, ArithmeticWithLoad)
{
    uint8_t data[] = { 0x05, 0x03, 0x08 };
    SDDL2_Input_cursor buffer;

    SDDL2_Input_cursor_init(&buffer, data, 3);

    // Load data[0] = 5
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(0)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_OK);

    // Load data[1] = 3
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(1)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_OK);

    // Add them
    ASSERT_EQ(SDDL2_op_add(stack_, NULL, 0), SDDL2_OK);

    // Result should be 8
    popAndVerifyI64(stack_, 8);

    // Verify it matches data[2]
    ASSERT_EQ(SDDL2_Stack_push(stack_, SDDL2_Value_i64(2)), SDDL2_OK);
    ASSERT_EQ(SDDL2_op_load_u8(stack_, &buffer), SDDL2_OK);
    popAndVerifyI64(stack_, 8);
}
