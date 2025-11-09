// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef SDDL2_TEST_FRAMEWORK_H
#define SDDL2_TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>
#include "openzl/compress/graphs/sddlv2/sddl2_interpreter.h"

/**
 * Simple test framework for SDDL2 tests
 *
 * Features:
 * - Auto-registration of tests using TEST() macro
 * - Automatic test discovery (no manual list in main)
 * - Helper macros for common test patterns
 * - Clean output formatting
 *
 * Usage:
 *   TEST(test_name) {
 *       // test code here
 *   }
 */

// Maximum number of tests that can be registered
#define SDDL2_MAX_TESTS 100

// Test function type
typedef void (*sddl2_test_func_t)(void);

// Test descriptor
typedef struct {
    const char* name;
    sddl2_test_func_t func;
} sddl2_test_descriptor_t;

// Global test registry
static sddl2_test_descriptor_t g_sddl2_tests[SDDL2_MAX_TESTS];
static int g_sddl2_test_count = 0;

/**
 * Register a test function at compile time
 *
 * Uses GCC/Clang __attribute__((constructor)) to run registration
 * before main() is called.
 */
#define SDDL2_REGISTER_TEST(test_func)                           \
    static void __sddl2_register_##test_func(void)               \
            __attribute__((constructor));                        \
    static void __sddl2_register_##test_func(void)               \
    {                                                            \
        if (g_sddl2_test_count < SDDL2_MAX_TESTS) {              \
            g_sddl2_tests[g_sddl2_test_count].name = #test_func; \
            g_sddl2_tests[g_sddl2_test_count].func = test_func;  \
            g_sddl2_test_count++;                                \
        }                                                        \
    }

/**
 * Define and register a test
 *
 * Example:
 *   TEST(test_simple_segment_creation) {
 *       // test code
 *   }
 */
#define TEST(name)             \
    static void name(void);    \
    SDDL2_REGISTER_TEST(name); \
    static void name(void)

/**
 * Helper macro for tests that expect success (SDDL2_OK)
 *
 * Sets up segments, executes bytecode, asserts OK, provides cleanup.
 *
 * Example:
 *   EXPECT_SUCCESS(bytecode, size, input, input_size) {
 *       assert(segments.count == 1);
 *       assert(segments.items[0].tag == 100);
 *   }
 */
#define EXPECT_SUCCESS(bytecode_ptr, bytecode_len, input_ptr, input_len)  \
    SDDL2_segment_list segments;                                          \
    SDDL2_segment_list_init(&segments, NULL, NULL);                       \
    SDDL2_error err = SDDL2_execute_bytecode(                             \
            bytecode_ptr, bytecode_len, input_ptr, input_len, &segments); \
    assert(err == SDDL2_OK);                                              \
    do

/**
 * Helper macro for cleanup after EXPECT_SUCCESS
 *
 * Must be called after EXPECT_SUCCESS block to clean up resources.
 */
#define END_EXPECT_SUCCESS() \
    while (0)                \
        ;                    \
    SDDL2_segment_list_destroy(&segments)

/**
 * Helper macro for tests that expect a specific error
 *
 * Sets up segments, executes bytecode, asserts expected error, provides
 * cleanup.
 *
 * Example:
 *   EXPECT_ERROR(SDDL2_DIV_ZERO, bytecode, size, input, input_size);
 */
#define EXPECT_ERROR(                                                         \
        expected_err, bytecode_ptr, bytecode_len, input_ptr, input_len)       \
    do {                                                                      \
        SDDL2_segment_list segments;                                          \
        SDDL2_segment_list_init(&segments, NULL, NULL);                       \
        SDDL2_error err = SDDL2_execute_bytecode(                             \
                bytecode_ptr, bytecode_len, input_ptr, input_len, &segments); \
        assert(err == expected_err);                                          \
        SDDL2_segment_list_destroy(&segments);                                \
    } while (0)

/**
 * Run all registered tests
 *
 * Returns 0 if all tests pass, 1 if any test fails.
 */
static inline int sddl2_run_all_tests(void)
{
    int failed = 0;

    printf("Running SDDL2 Tests...\n\n");

    for (int i = 0; i < g_sddl2_test_count; i++) {
        const sddl2_test_descriptor_t* test = &g_sddl2_tests[i];

        // Run test
        test->func();

        // Print success (test would have asserted on failure)
        printf("✓ %s passed\n", test->name);
    }

    printf("\n✅ All tests passed! (%d tests)\n", g_sddl2_test_count);

    return failed;
}

#endif // SDDL2_TEST_FRAMEWORK_H
