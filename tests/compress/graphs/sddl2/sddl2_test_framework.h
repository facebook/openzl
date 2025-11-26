// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef SDDL2_TEST_FRAMEWORK_H
#define SDDL2_TEST_FRAMEWORK_H

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "openzl/compress/graphs/sddl2/sddl2_vm.h"

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
#define TEST(name)            \
    static void name(void);   \
    SDDL2_REGISTER_TEST(name) \
    static void name(void)

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

/* ============================================================================
 * Common Test Setup Helpers
 * ========================================================================= */

/* Helper: Assert value is I64 with expected result */
#define ASSERT_I64_VALUE(val, expected)           \
    do {                                          \
        assert((val).kind == SDDL2_VALUE_I64);    \
        assert((val).value.as_i64 == (expected)); \
    } while (0)

/* Helper: Pop and verify I64 result */
#define POP_AND_VERIFY_I64(stack, expected_value)             \
    do {                                                      \
        SDDL2_Value _result;                                  \
        assert(SDDL2_Stack_pop(stack, &_result) == SDDL2_OK); \
        ASSERT_I64_VALUE(_result, expected_value);            \
    } while (0)

/* Helper: Setup input buffer for tests */
#define SETUP_INPUT_BUFFER(buffer_var, data_array) \
    SDDL2_Input_cursor buffer_var;                 \
    SDDL2_Input_cursor_init(&buffer_var, data_array, sizeof(data_array))

/* Helper: Setup segment list for tests */
#define SETUP_SEGMENT_LIST(segments_var) \
    SDDL2_Segment_list segments_var;     \
    SDDL2_Segment_list_init(&segments_var, NULL, NULL)

/* Helper: Setup tag registry for tests */
#define SETUP_TAG_REGISTRY(registry_var) \
    SDDL2_Tag_registry registry_var;     \
    SDDL2_Tag_registry_init(&registry_var, NULL, NULL)

/* Helper: Cleanup segment list */
#define CLEANUP_SEGMENT_LIST(segments_var) \
    SDDL2_Segment_list_destroy(&segments_var)

/* Helper: Cleanup tag registry */
#define CLEANUP_TAG_REGISTRY(registry_var) \
    SDDL2_Tag_registry_destroy(&registry_var)

#endif // SDDL2_TEST_FRAMEWORK_H
