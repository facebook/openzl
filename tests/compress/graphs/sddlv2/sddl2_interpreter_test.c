// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Unit tests for SDDL2 bytecode interpreter.
 * Tests the minimal end-to-end execution: bytecode -> segments
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "openzl/compress/graphs/sddlv2/sddl2_interpreter.h"

/**
 * Test: Execute simple program that creates one unspecified segment
 *
 * Assembly:
 *   push.i32 5
 *   segment.create_unspecified
 *   halt
 *
 * Bytecode (little-endian):
 *   03 00 01 00   # push.i32 (opcode=0x0003, family=0x0001)
 *   05 00 00 00   # value = 5
 *   01 00 0C 00   # segment.create_unspecified (opcode=0x0001, family=0x000C)
 *   01 00 05 00   # halt (opcode=0x0001, family=0x0005)
 */
static void test_simple_segment_creation(void)
{
    // Input data: "Hello"
    uint8_t input[] = "Hello";

    // Bytecode: push.i32 5, segment.create_unspecified, halt
    uint8_t bytecode[] = {
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x05, 0x00, 0x00, 0x00, // value = 5
        0x01, 0x00, 0x0C, 0x00, // segment.create_unspecified
        0x01, 0x00, 0x05, 0x00  // halt
    };

    // Execute
    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode,
            sizeof(bytecode),
            input,
            sizeof(input) - 1, // Exclude null terminator
            &segments);

    // Verify
    assert(err == SDDL2_OK);
    assert(segments.count == 1);
    assert(segments.items[0].tag == 0); // Unspecified segment
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 5);

    // Cleanup
    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_simple_segment_creation passed\n");
}

/**
 * Test: Push zero and create zero-size segment
 */
static void test_zero_size_segment(void)
{
    uint8_t input[] = "Test";

    // Bytecode: push.zero, segment.create_unspecified, halt
    uint8_t bytecode[] = {
        0x01, 0x00, 0x01, 0x00, // push.zero
        0x01, 0x00, 0x0C, 0x00, // segment.create_unspecified
        0x01, 0x00, 0x05, 0x00  // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 1);
    assert(segments.items[0].size_bytes == 0);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_zero_size_segment passed\n");
}

/**
 * Test: Multiple segments
 */
static void test_multiple_segments(void)
{
    uint8_t input[] = "HelloWorld";

    // Bytecode:
    //   push.i32 5, segment.create_unspecified,  # "Hello"
    //   push.i32 5, segment.create_unspecified,  # "World"
    //   halt
    uint8_t bytecode[] = {
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x05, 0x00, 0x00, 0x00, // 5
        0x01, 0x00, 0x0C, 0x00, // segment.create_unspecified

        0x03, 0x00, 0x01, 0x00, // push.i32
        0x05, 0x00, 0x00, 0x00, // 5
        0x01, 0x00, 0x0C, 0x00, // segment.create_unspecified

        0x01, 0x00, 0x05, 0x00 // halt
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err == SDDL2_OK);
    assert(segments.count == 2);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 5);
    assert(segments.items[1].start_pos == 5);
    assert(segments.items[1].size_bytes == 5);

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_multiple_segments passed\n");
}

/**
 * Test: Invalid bytecode (not multiple of 4)
 */
static void test_invalid_bytecode_size(void)
{
    uint8_t input[]    = "Test";
    uint8_t bytecode[] = { 0x01, 0x00, 0x03 }; // Only 3 bytes

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err != SDDL2_OK); // Should fail

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_invalid_bytecode_size passed\n");
}

/**
 * Test: Program without halt
 */
static void test_missing_halt(void)
{
    uint8_t input[] = "Test";

    // Bytecode: push.i32 5, segment.create_unspecified (no halt!)
    uint8_t bytecode[] = {
        0x03, 0x00, 0x01, 0x00, // push.i32
        0x05, 0x00, 0x00, 0x00, // 5
        0x01, 0x00, 0x0C, 0x00  // segment.create_unspecified
    };

    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, sizeof(bytecode), input, sizeof(input) - 1, &segments);

    assert(err != SDDL2_OK); // Should fail - no halt

    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_missing_halt passed\n");
}

int main(void)
{
    printf("Running SDDL2 Interpreter Tests...\n\n");

    test_simple_segment_creation();
    test_zero_size_segment();
    test_multiple_segments();
    test_invalid_bytecode_size();
    test_missing_halt();

    printf("\n✅ All interpreter tests passed! (5 tests)\n");
    return 0;
}
