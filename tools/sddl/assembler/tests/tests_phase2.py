#!/usr/bin/env python3
"""
Test suite for OpenZL assembler Phase 2
Tests push.u32, push.i32, push.i64 with various formats and edge cases
"""

import subprocess
import sys

# Configuration
ASSEMBLER_PATH = 'assembler.py'


def run_test(name: str, asm_code: str, expected_hex: str):
    """Run a single test case"""
    print(f"Testing: {name}...", end=" ")
    
    # Assemble
    result = subprocess.run(
        ['python3', ASSEMBLER_PATH, '-c', asm_code],
        capture_output=True,
        text=True
    )
    
    if result.returncode != 0:
        print(f"FAIL - Assembler error: {result.stdout}{result.stderr}")
        return False
    
    # Compare output
    actual_hex = result.stdout.strip()
    if actual_hex == expected_hex:
        print("PASS")
        return True
    else:
        print(f"FAIL")
        print(f"  Expected: {expected_hex}")
        print(f"  Got:      {actual_hex}")
        return False


def run_error_test(name: str, asm_code: str, expected_error_substring: str):
    """Run a test that expects an error"""
    print(f"Testing: {name}...", end=" ")
    
    result = subprocess.run(
        ['python3', ASSEMBLER_PATH, '-c', asm_code],
        capture_output=True,
        text=True
    )
    
    if result.returncode == 0:
        print(f"FAIL - Expected error but succeeded")
        return False
    
    error_output = result.stdout + result.stderr
    if expected_error_substring.lower() in error_output.lower():
        print("PASS")
        return True
    else:
        print(f"FAIL")
        print(f"  Expected error containing: {expected_error_substring}")
        print(f"  Got: {error_output}")
        return False


def main():
    passed = 0
    failed = 0
    
    # ===== Phase 1 Regression Tests =====
    phase1_tests = [
        ("halt", "halt", "05 00 01 00"),
        ("push.zero", "push.zero halt", "01 00 01 00 05 00 01 00"),
    ]
    
    print("=== Phase 1 Regression Tests ===")
    for name, code, expected in phase1_tests:
        if run_test(name, code, expected):
            passed += 1
        else:
            failed += 1
    
    # ===== Phase 2: push.u32 Tests =====
    push_u32_tests = [
        ("push.u32 0", "push.u32 0 halt",
         "01 00 02 00 00 00 00 00 05 00 01 00"),
        ("push.u32 42", "push.u32 42 halt",
         "01 00 02 00 2a 00 00 00 05 00 01 00"),
        ("push.u32 255", "push.u32 255 halt",
         "01 00 02 00 ff 00 00 00 05 00 01 00"),
        ("push.u32 max", "push.u32 4294967295 halt",
         "01 00 02 00 ff ff ff ff 05 00 01 00"),
        ("push.u32 hex", "push.u32 0x2A halt",
         "01 00 02 00 2a 00 00 00 05 00 01 00"),
        ("push.u32 binary", "push.u32 0b101010 halt",
         "01 00 02 00 2a 00 00 00 05 00 01 00"),
    ]
    
    print("\n=== Phase 2: push.u32 Tests ===")
    for name, code, expected in push_u32_tests:
        if run_test(name, code, expected):
            passed += 1
        else:
            failed += 1
    
    # ===== Phase 2: push.i32 Tests =====
    push_i32_tests = [
        ("push.i32 0", "push.i32 0 halt",
         "01 00 03 00 00 00 00 00 05 00 01 00"),
        ("push.i32 42", "push.i32 42 halt",
         "01 00 03 00 2a 00 00 00 05 00 01 00"),
        ("push.i32 -1", "push.i32 -1 halt",
         "01 00 03 00 ff ff ff ff 05 00 01 00"),
        ("push.i32 -100", "push.i32 -100 halt",
         "01 00 03 00 9c ff ff ff 05 00 01 00"),
        ("push.i32 max", "push.i32 2147483647 halt",
         "01 00 03 00 ff ff ff 7f 05 00 01 00"),
        ("push.i32 min", "push.i32 -2147483648 halt",
         "01 00 03 00 00 00 00 80 05 00 01 00"),
        ("push.i32 hex negative", "push.i32 -0xFF halt",
         "01 00 03 00 01 ff ff ff 05 00 01 00"),
    ]
    
    print("\n=== Phase 2: push.i32 Tests ===")
    for name, code, expected in push_i32_tests:
        if run_test(name, code, expected):
            passed += 1
        else:
            failed += 1
    
    # ===== Phase 2: push.i64 Tests =====
    push_i64_tests = [
        ("push.i64 0", "push.i64 0 halt",
         "01 00 04 00 00 00 00 00 00 00 00 00 05 00 01 00"),
        ("push.i64 1000000", "push.i64 1000000 halt",
         "01 00 04 00 40 42 0f 00 00 00 00 00 05 00 01 00"),
        ("push.i64 -1", "push.i64 -1 halt",
         "01 00 04 00 ff ff ff ff ff ff ff ff 05 00 01 00"),
        ("push.i64 max", "push.i64 9223372036854775807 halt",
         "01 00 04 00 ff ff ff ff ff ff ff 7f 05 00 01 00"),
        ("push.i64 min", "push.i64 -9223372036854775808 halt",
         "01 00 04 00 00 00 00 00 00 00 00 80 05 00 01 00"),
    ]
    
    print("\n=== Phase 2: push.i64 Tests ===")
    for name, code, expected in push_i64_tests:
        if run_test(name, code, expected):
            passed += 1
        else:
            failed += 1
    
    # ===== Multiple Push Tests =====
    multi_tests = [
        ("mixed pushes", "push.u32 100 push.i32 -50 push.i64 1000000 halt",
         "01 00 02 00 64 00 00 00 01 00 03 00 ce ff ff ff 01 00 04 00 40 42 0f 00 00 00 00 00 05 00 01 00"),
        ("push.zero + push.u32", "push.zero push.u32 42 halt",
         "01 00 01 00 01 00 02 00 2a 00 00 00 05 00 01 00"),
    ]
    
    print("\n=== Multiple Push Tests ===")
    for name, code, expected in multi_tests:
        if run_test(name, code, expected):
            passed += 1
        else:
            failed += 1
    
    # ===== Error Tests =====
    error_tests = [
        ("push.u32 negative", "push.u32 -1 halt", "out of range"),
        ("push.u32 too large", "push.u32 4294967296 halt", "out of range"),
        ("push.i32 too large", "push.i32 2147483648 halt", "out of range"),
        ("push.i32 too small", "push.i32 -2147483649 halt", "out of range"),
        ("push.i64 too large", "push.i64 9223372036854775808 halt", "out of range"),
        ("push.u32 missing param", "push.u32 halt", "requires 1 parameter"),
        ("push.i32 missing param", "push.i32 halt", "requires 1 parameter"),
        ("push.i64 missing param", "push.i64 halt", "requires 1 parameter"),
        ("push.zero with param", "push.zero 42 halt", "unknown instruction"),  # 42 is not an instruction
        ("invalid literal", "push.u32 xyz halt", "invalid integer"),
    ]
    
    print("\n=== Error Handling Tests ===")
    for name, code, expected_error in error_tests:
        if run_error_test(name, code, expected_error):
            passed += 1
        else:
            failed += 1
    
    # ===== Summary =====
    print(f"\n{'='*50}")
    print(f"Total: {passed} passed, {failed} failed")
    print(f"{'='*50}")
    
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
