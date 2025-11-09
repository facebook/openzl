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
 *   python3 tools/sddl/assembler/sddl2_assembler.py test_hello.sddl test_hello.bin
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

int main(void)
{
    printf("Running SDDL2 Bytecode Execution Test...\n\n");

    test_assembled_hello();

    printf("\n✅ Bytecode execution test passed!\n");
    printf("Verified: Load bytecode → Execute → Verify segments\n");
    return 0;
}
