// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * Bytecode Execution Test
 *
 * This test verifies loading and executing pre-assembled bytecode:
 * 1. Load bytecode from .bin file (assembled externally)
 * 2. Execute bytecode through the interpreter
 * 3. Verify segment output
 *
 * Note: This is NOT a true end-to-end test - the assembly step is manual.
 * The bytecode must be pre-generated using the assembler:
 *   python3 tools/sddl/assembler/sddl2_assembler.py test_hello.sddl
 * test_hello.bin
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openzl/compress/graphs/sddl2/sddl2_interpreter.h"

/**
 * Load bytecode from file
 */
static uint8_t* load_bytecode(const char* filename, size_t* size_out)
{
    FILE* f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open %s\n", filename);
        return NULL;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return NULL;
    }

    // Allocate and read
    uint8_t* bytecode = (uint8_t*)malloc((size_t)size);
    if (!bytecode) {
        fclose(f);
        return NULL;
    }

    size_t bytes_read = fread(bytecode, 1, (size_t)size, f);
    fclose(f);

    if (bytes_read != (size_t)size) {
        free(bytecode);
        return NULL;
    }

    *size_out = (size_t)size;
    return bytecode;
}

/**
 * Test: Load and execute assembled bytecode from test_hello.bin
 *
 * Assembly source (test_hello.sddl):
 *   push.i32 5
 *   segment.create_unspecified
 *   halt
 *
 * Expected behavior:
 *   - Creates 1 unspecified segment of size 5
 *   - Segment starts at offset 0
 */
static void test_assembled_hello(void)
{
    // Input data
    uint8_t input[] = "Hello";

    // Load assembled bytecode
    size_t bytecode_size;
    uint8_t* bytecode = load_bytecode("test_hello.bin", &bytecode_size);

    if (!bytecode) {
        fprintf(stderr, "Failed to load test_hello.bin\n");
        fprintf(stderr,
                "Make sure to run: python3 "
                "../../../tools/sddl/assembler/sddl2_assembler.py test_hello.sddl "
                "test_hello.bin\n");
        exit(1);
    }

    printf("Loaded %zu bytes of bytecode from test_hello.bin\n", bytecode_size);

    // Execute bytecode
    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input) - 1, &segments);

    // Verify execution
    assert(err == SDDL2_OK);
    assert(segments.count == 1);
    assert(segments.items[0].tag == 0); // Unspecified
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 5);

    // Cleanup
    free(bytecode);
    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_assembled_hello passed\n");
}

/**
 * Test: Load and execute complex multi-segment bytecode
 *
 * Assembly source (test_multi_segments.sddl):
 *   # Segment 1: U8 array (tag=100), 4 bytes at offset 0
 *   push.i32 4
 *   push.tag 100
 *   push.type.u8
 *   segment.create_tagged
 *
 *   # Segment 2: I32LE scalar (tag=200), 4 bytes at offset 4
 *   push.i32 4
 *   push.tag 200
 *   push.type.i32le
 *   segment.create_tagged
 *
 *   # Segment 3: Unspecified (tag=0), 4 bytes at offset 8
 *   push.i32 4
 *   segment.create_unspecified
 *   halt
 *
 * Expected behavior:
 *   - Creates 3 segments with different types and tags
 *   - Tests both tagged and unspecified segments
 *   - Tests different type kinds (U8, I32LE)
 */
static void test_multi_segments(void)
{
    // Input data: 12 bytes [01 02 03 04][05 06 07 08][09 0A 0B 0C]
    uint8_t input[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                        0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C };

    // Load assembled bytecode
    size_t bytecode_size;
    uint8_t* bytecode =
            load_bytecode("test_multi_segments.bin", &bytecode_size);

    if (!bytecode) {
        fprintf(stderr, "Failed to load test_multi_segments.bin\n");
        fprintf(stderr,
                "Make sure to run: python3 "
                "../../../tools/sddl/assembler/sddl2_assembler.py "
                "test_multi_segments.sddl test_multi_segments.bin\n");
        exit(1);
    }

    printf("Loaded %zu bytes of bytecode from test_multi_segments.bin\n",
           bytecode_size);

    // Execute bytecode
    SDDL2_segment_list segments;
    SDDL2_segment_list_init(&segments, NULL, NULL);

    SDDL2_error err = SDDL2_execute_bytecode(
            bytecode, bytecode_size, input, sizeof(input), &segments);

    // Verify execution
    if (err != SDDL2_OK) {
        fprintf(stderr, "Execution failed with error code: %d\n", err);
        free(bytecode);
        SDDL2_segment_list_destroy(&segments);
        exit(1);
    }
    assert(err == SDDL2_OK);
    assert(segments.count == 3);

    // Segment 1: U8 array, tag=100, 4 bytes starting at offset 0
    assert(segments.items[0].tag == 100);
    assert(segments.items[0].start_pos == 0);
    assert(segments.items[0].size_bytes == 4);
    assert(segments.items[0].type.kind == SDDL2_TYPE_U8);
    assert(segments.items[0].type.width == 1);

    // Segment 2: I32LE scalar, tag=200, 4 bytes starting at offset 4
    assert(segments.items[1].tag == 200);
    assert(segments.items[1].start_pos == 4);
    assert(segments.items[1].size_bytes == 4);
    assert(segments.items[1].type.kind == SDDL2_TYPE_I32LE);
    assert(segments.items[1].type.width == 1);

    // Segment 3: Unspecified, tag=0, 4 bytes starting at offset 8
    assert(segments.items[2].tag == 0);
    assert(segments.items[2].start_pos == 8);
    assert(segments.items[2].size_bytes == 4);
    // Unspecified segments have type = BYTES
    assert(segments.items[2].type.kind == SDDL2_TYPE_BYTES);

    // Cleanup
    free(bytecode);
    SDDL2_segment_list_destroy(&segments);

    printf("✓ test_multi_segments passed\n");
}

int main(void)
{
    printf("Running SDDL2 Bytecode Execution Test...\n\n");

    test_assembled_hello();
    test_multi_segments();

    printf("\n✅ All bytecode execution tests passed!\n");
    printf("Verified: Load bytecode → Execute → Verify segments\n");
    return 0;
}
