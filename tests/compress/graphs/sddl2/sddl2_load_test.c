// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Comprehensive tests for SDDL2 LOAD operations
 * Tests all integer load opcodes with various data patterns
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>  // printf
#include <stdlib.h> // malloc, free
#include <string.h> // memcpy
#include "openzl/compress/graphs/sddl2/sddl2_vm.h"
#include "sddl2_test_framework.h"

/* ============================================================================
 * Test Setup Helpers
 * ========================================================================= */

static SDDL2_Stack* create_test_stack(size_t capacity)
{
    SDDL2_Stack* stack = malloc(sizeof(SDDL2_Stack));
    assert(stack != NULL);

    stack->items = malloc(sizeof(SDDL2_Value) * capacity);
    assert(stack->items != NULL);

    stack->capacity = capacity;
    SDDL2_Stack_init(stack);

    return stack;
}

static void destroy_test_stack(SDDL2_Stack* stack)
{
    free(stack->items);
    free(stack);
}

/* ============================================================================
 * 8-bit Load Tests
 * ========================================================================= */

TEST(test_load_u8_basic)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0xFF, 0x00, 0x7F };
    SETUP_INPUT_BUFFER(buffer, data);

    // Load byte at address 0: 0x42 (66)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0x42);

    // Load byte at address 1: 0xFF (255, zero-extended)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0xFF);

    destroy_test_stack(stack);
}

TEST(test_load_i8_sign_extension)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0xFF, 0x80, 0x7F };
    SETUP_INPUT_BUFFER(buffer, data);

    // Load signed byte at address 0: 0x42 (66, positive)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_i8(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 66);

    // Load signed byte at address 1: 0xFF (-1, sign-extended)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_load_i8(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, -1);

    // Load signed byte at address 2: 0x80 (-128, sign-extended)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(2)) == SDDL2_OK);
    assert(SDDL2_op_load_i8(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, -128);

    // Load signed byte at address 3: 0x7F (127, positive)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(3)) == SDDL2_OK);
    assert(SDDL2_op_load_i8(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 127);

    destroy_test_stack(stack);
}

/* ============================================================================
 * 16-bit Load Tests
 * ========================================================================= */

TEST(test_load_u16le_basic)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x34, 0x12, 0xFF, 0xFF }; // 0x1234 LE, 0xFFFF LE
    SETUP_INPUT_BUFFER(buffer, data);

    // Load u16le at address 0: 0x1234
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_u16le(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0x1234);

    // Load u16le at address 2: 0xFFFF (65535, zero-extended)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(2)) == SDDL2_OK);
    assert(SDDL2_op_load_u16le(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0xFFFF);

    destroy_test_stack(stack);
}

TEST(test_load_u16be_basic)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x12, 0x34, 0xFF, 0xFF }; // 0x1234 BE, 0xFFFF BE
    SETUP_INPUT_BUFFER(buffer, data);

    // Load u16be at address 0: 0x1234
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_u16be(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0x1234);

    destroy_test_stack(stack);
}

TEST(test_load_i16le_sign_extension)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0xFF, 0xFF, 0xFF, 0x7F }; // -1 LE, 32767 LE
    SETUP_INPUT_BUFFER(buffer, data);

    // Load i16le at address 0: 0xFFFF (-1, sign-extended)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_i16le(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, -1);

    // Load i16le at address 2: 0x7FFF (32767, positive)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(2)) == SDDL2_OK);
    assert(SDDL2_op_load_i16le(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 32767);

    destroy_test_stack(stack);
}

TEST(test_load_i16be_sign_extension)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0xFF, 0xFF, 0x7F, 0xFF }; // -1 BE, 32767 BE
    SETUP_INPUT_BUFFER(buffer, data);

    // Load i16be at address 0: 0xFFFF (-1, sign-extended)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_i16be(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, -1);

    destroy_test_stack(stack);
}

/* ============================================================================
 * 32-bit Load Tests
 * ========================================================================= */

TEST(test_load_u32le_basic)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x78, 0x56, 0x34, 0x12 }; // 0x12345678 LE
    SETUP_INPUT_BUFFER(buffer, data);

    // Load u32le at address 0: 0x12345678
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_u32le(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0x12345678);

    destroy_test_stack(stack);
}

TEST(test_load_u32be_basic)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x12, 0x34, 0x56, 0x78 }; // 0x12345678 BE
    SETUP_INPUT_BUFFER(buffer, data);

    // Load u32be at address 0: 0x12345678
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_u32be(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0x12345678);

    destroy_test_stack(stack);
}

TEST(test_load_i32le_sign_extension)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F };
    SETUP_INPUT_BUFFER(buffer, data);

    // Load i32le at address 0: 0xFFFFFFFF (-1, sign-extended)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_i32le(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, -1);

    // Load i32le at address 4: 0x7FFFFFFF (2147483647, positive)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(4)) == SDDL2_OK);
    assert(SDDL2_op_load_i32le(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 2147483647);

    destroy_test_stack(stack);
}

TEST(test_load_i32be_sign_extension)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0xFF, 0xFF, 0xFF, 0xFF }; // -1 BE
    SETUP_INPUT_BUFFER(buffer, data);

    // Load i32be at address 0: 0xFFFFFFFF (-1, sign-extended)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_i32be(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, -1);

    destroy_test_stack(stack);
}

/* ============================================================================
 * 64-bit Load Tests
 * ========================================================================= */

TEST(test_load_i64le_basic)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12 };
    SETUP_INPUT_BUFFER(buffer, data);

    // Load i64le at address 0: 0x1234567890ABCDEF
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_i64le(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0x1234567890ABCDEF);

    destroy_test_stack(stack);
}

TEST(test_load_i64be_basic)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF };
    SETUP_INPUT_BUFFER(buffer, data);

    // Load i64be at address 0: 0x1234567890ABCDEF
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_i64be(stack, &buffer) == SDDL2_OK);
    POP_AND_VERIFY_I64(stack, 0x1234567890ABCDEF);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Error Condition Tests - Stack Underflow
 * ========================================================================= */

TEST(test_load_u8_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_load_i8_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_op_load_i8(stack, &buffer) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_load_u16le_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0x43 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_op_load_u16le(stack, &buffer) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_load_u16be_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0x43 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_op_load_u16be(stack, &buffer) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_load_i16le_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0x43 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_op_load_i16le(stack, &buffer) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_load_i16be_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0x43 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_op_load_i16be(stack, &buffer) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_load_u32le_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_op_load_u32le(stack, &buffer) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_load_u32be_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_op_load_u32be(stack, &buffer) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_load_i32le_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_op_load_i32le(stack, &buffer) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_load_i32be_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_op_load_i32be(stack, &buffer) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_load_i64le_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_op_load_i64le(stack, &buffer) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

TEST(test_load_i64be_stack_underflow)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_op_load_i64be(stack, &buffer) == SDDL2_STACK_UNDERFLOW);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Error Condition Tests - Type Mismatch
 * ========================================================================= */

TEST(test_load_u8_type_mismatch_tag)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(100)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_u8_type_mismatch_type)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42 };
    SETUP_INPUT_BUFFER(buffer, data);

    SDDL2_Type type = { .kind = SDDL2_TYPE_U8, .width = 1 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_i8_type_mismatch_tag)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(200)) == SDDL2_OK);
    assert(SDDL2_op_load_i8(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_i8_type_mismatch_type)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42 };
    SETUP_INPUT_BUFFER(buffer, data);

    SDDL2_Type type = { .kind = SDDL2_TYPE_I8, .width = 1 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_load_i8(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_u16le_type_mismatch_tag)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0x43 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(300)) == SDDL2_OK);
    assert(SDDL2_op_load_u16le(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_u16le_type_mismatch_type)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0x43 };
    SETUP_INPUT_BUFFER(buffer, data);

    SDDL2_Type type = { .kind = SDDL2_TYPE_U16LE, .width = 2 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_load_u16le(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_u16be_type_mismatch_tag)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0x43 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(400)) == SDDL2_OK);
    assert(SDDL2_op_load_u16be(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_u16be_type_mismatch_type)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0x43 };
    SETUP_INPUT_BUFFER(buffer, data);

    SDDL2_Type type = { .kind = SDDL2_TYPE_U16BE, .width = 2 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_load_u16be(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_i16le_type_mismatch_tag)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0x43 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(500)) == SDDL2_OK);
    assert(SDDL2_op_load_i16le(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_i16le_type_mismatch_type)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0x43 };
    SETUP_INPUT_BUFFER(buffer, data);

    SDDL2_Type type = { .kind = SDDL2_TYPE_I16LE, .width = 2 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_load_i16le(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_i16be_type_mismatch_tag)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0x43 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(600)) == SDDL2_OK);
    assert(SDDL2_op_load_i16be(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_i16be_type_mismatch_type)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0x43 };
    SETUP_INPUT_BUFFER(buffer, data);

    SDDL2_Type type = { .kind = SDDL2_TYPE_I16BE, .width = 2 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_load_i16be(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_u32le_type_mismatch_tag)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(700)) == SDDL2_OK);
    assert(SDDL2_op_load_u32le(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_u32le_type_mismatch_type)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04 };
    SETUP_INPUT_BUFFER(buffer, data);

    SDDL2_Type type = { .kind = SDDL2_TYPE_U32LE, .width = 4 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_load_u32le(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_u32be_type_mismatch_tag)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(800)) == SDDL2_OK);
    assert(SDDL2_op_load_u32be(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_u32be_type_mismatch_type)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04 };
    SETUP_INPUT_BUFFER(buffer, data);

    SDDL2_Type type = { .kind = SDDL2_TYPE_U32BE, .width = 4 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_load_u32be(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_i32le_type_mismatch_tag)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(900)) == SDDL2_OK);
    assert(SDDL2_op_load_i32le(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_i32le_type_mismatch_type)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04 };
    SETUP_INPUT_BUFFER(buffer, data);

    SDDL2_Type type = { .kind = SDDL2_TYPE_I32LE, .width = 4 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_load_i32le(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_i32be_type_mismatch_tag)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(1000)) == SDDL2_OK);
    assert(SDDL2_op_load_i32be(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_i32be_type_mismatch_type)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04 };
    SETUP_INPUT_BUFFER(buffer, data);

    SDDL2_Type type = { .kind = SDDL2_TYPE_I32BE, .width = 4 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_load_i32be(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_i64le_type_mismatch_tag)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(1100)) == SDDL2_OK);
    assert(SDDL2_op_load_i64le(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_i64le_type_mismatch_type)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    SETUP_INPUT_BUFFER(buffer, data);

    SDDL2_Type type = { .kind = SDDL2_TYPE_I64LE, .width = 8 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_load_i64le(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_i64be_type_mismatch_tag)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_tag(1200)) == SDDL2_OK);
    assert(SDDL2_op_load_i64be(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

TEST(test_load_i64be_type_mismatch_type)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    SETUP_INPUT_BUFFER(buffer, data);

    SDDL2_Type type = { .kind = SDDL2_TYPE_I64BE, .width = 8 };
    assert(SDDL2_Stack_push(stack, SDDL2_Value_type(type)) == SDDL2_OK);
    assert(SDDL2_op_load_i64be(stack, &buffer) == SDDL2_TYPE_MISMATCH);

    destroy_test_stack(stack);
}

/* ============================================================================
 * Error Condition Tests - Bounds Checking
 * ========================================================================= */

TEST(test_load_u8_bounds_negative_address)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(-1)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
}

TEST(test_load_u8_bounds_beyond_buffer)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_load_u8(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
}

TEST(test_load_i8_bounds_beyond_buffer)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_load_i8(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
}

TEST(test_load_u16le_bounds_beyond_buffer)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0x43 };
    SETUP_INPUT_BUFFER(buffer, data);

    // Try to load at address 1 (needs 2 bytes, but only 1 byte left)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_load_u16le(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
}

TEST(test_load_u16be_bounds_beyond_buffer)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0x43 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_load_u16be(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
}

TEST(test_load_i16le_bounds_beyond_buffer)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0x43 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_load_i16le(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
}

TEST(test_load_i16be_bounds_beyond_buffer)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x42, 0x43 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(1)) == SDDL2_OK);
    assert(SDDL2_op_load_i16be(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
}

TEST(test_load_u32le_bounds_beyond_buffer)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03 }; // Only 3 bytes
    SETUP_INPUT_BUFFER(buffer, data);

    // Try to load u32le at address 0 (needs 4 bytes, but only have 3)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_u32le(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
}

TEST(test_load_u32be_bounds_beyond_buffer)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_u32be(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
}

TEST(test_load_i32le_bounds_beyond_buffer)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_i32le(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
}

TEST(test_load_i32be_bounds_beyond_buffer)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_i32be(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
}

TEST(test_load_i64le_bounds_beyond_buffer)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
    }; // Only 7 bytes
    SETUP_INPUT_BUFFER(buffer, data);

    // Try to load i64le at address 0 (needs 8 bytes, but only have 7)
    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_i64le(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
}

TEST(test_load_i64be_bounds_beyond_buffer)
{
    SDDL2_Stack* stack = create_test_stack(100);
    uint8_t data[]     = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    SETUP_INPUT_BUFFER(buffer, data);

    assert(SDDL2_Stack_push(stack, SDDL2_Value_i64(0)) == SDDL2_OK);
    assert(SDDL2_op_load_i64be(stack, &buffer) == SDDL2_LOAD_BOUNDS);

    destroy_test_stack(stack);
}

int main(void)
{
    return sddl2_run_all_tests();
}
