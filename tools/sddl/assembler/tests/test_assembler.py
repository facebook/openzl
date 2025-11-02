#!/usr/bin/env python3
"""
Test suite for OpenZL assembler Phase 1
"""

import subprocess
import sys

# Configuration
ASSEMBLER_PATH = '../assembler.py'


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


def main():
    tests = [
        ("empty program", "", ""),
        ("halt", "halt", "05 00 01 00"),
        ("push.zero", "push.zero halt", "01 00 01 00 05 00 01 00"),
        ("three zeros", "push.zero push.zero push.zero halt", 
         "01 00 01 00 01 00 01 00 01 00 01 00 05 00 01 00"),
        ("with comments", "push.zero # comment\nhalt", "01 00 01 00 05 00 01 00"),
    ]
    
    passed = 0
    failed = 0
    
    for name, code, expected in tests:
        if run_test(name, code, expected):
            passed += 1
        else:
            failed += 1
    
    print(f"\n{passed} passed, {failed} failed")
    
    # Test error handling
    print("\nTesting error handling...", end=" ")
    result = subprocess.run(
        ['python3', ASSEMBLER_PATH, '-c', 'invalid'],
        capture_output=True,
        text=True
    )
    if result.returncode != 0 and "Unknown instruction" in result.stdout:
        print("PASS")
        passed += 1
    else:
        print("FAIL - Expected error not raised")
        failed += 1
    
    print(f"\nTotal: {passed} passed, {failed} failed")
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
