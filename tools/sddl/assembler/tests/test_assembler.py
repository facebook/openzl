#!/usr/bin/env python3
"""
Test suite for OpenZL assembler
Auto-discovers and runs all .asm files in tests/success/
"""

import os
import subprocess
import sys
from pathlib import Path

# Configuration
SCRIPT_DIR = Path(__file__).resolve().parent
ASSEMBLER_PATH = SCRIPT_DIR.parent / 'sddl2_assembler.py'
SUCCESS_DIR = SCRIPT_DIR / 'success'
FAIL_DIR = SCRIPT_DIR / 'fail'


def run_success_test(asm_file: Path, expected_file: Path):
    """Run a test that should succeed"""
    test_name = asm_file.stem
    print(f"Testing: {test_name}...", end=" ")
    
    # Check if expected file exists
    if not expected_file.exists():
        print(f"SKIP - No .expected file (run regenerate_expected.sh)")
        return None
    
    # Read expected output
    expected_hex = expected_file.read_text().strip()
    
    # Read source code and use -c mode to avoid creating .bin files
    asm_source = asm_file.read_text()
    
    # Assemble using -c mode
    result = subprocess.run(
        ['python3', str(ASSEMBLER_PATH), '-c', asm_source],
        capture_output=True,
        text=True
    )
    
    if result.returncode != 0:
        print(f"FAIL - Assembler error:")
        print(f"  {result.stdout}{result.stderr}")
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


def run_fail_test(asm_file: Path):
    """Run a test that should fail"""
    test_name = asm_file.stem
    print(f"Testing: {test_name}...", end=" ")
    
    # Read source code and look for EXPECT-ERROR comment
    asm_source = asm_file.read_text()
    expected_error = None
    
    for line in asm_source.split('\n'):
        stripped = line.strip()
        if stripped.startswith('# EXPECT-ERROR:'):
            expected_error = stripped.split(':', 1)[1].strip()
            break
    
    # Assemble using -c mode
    result = subprocess.run(
        ['python3', str(ASSEMBLER_PATH), '-c', asm_source],
        capture_output=True,
        text=True
    )
    
    # Should fail
    if result.returncode == 0:
        print(f"FAIL - Expected assembler to fail but it succeeded")
        return False
    
    # If specific error expected, verify it
    if expected_error:
        error_output = result.stdout + result.stderr
        if expected_error.lower() in error_output.lower():
            print("PASS")
            return True
        else:
            print(f"FAIL - Expected error containing: '{expected_error}'")
            print(f"  Got: {error_output.strip()}")
            return False
    else:
        # Just verify it failed
        print("PASS")
        return True


def main():
    if not ASSEMBLER_PATH.exists():
        print(f"Error: Assembler not found at {ASSEMBLER_PATH}")
        return 1
    
    passed = 0
    failed = 0
    skipped = 0
    
    # Run success tests
    if SUCCESS_DIR.exists():
        success_files = sorted(SUCCESS_DIR.glob('*.asm'))
        
        if success_files:
            print(f"=== Success Tests ({SUCCESS_DIR}) ===")
            print(f"Found {len(success_files)} test files\n")
            
            for asm_file in success_files:
                expected_file = asm_file.with_suffix('.expected')
                result = run_success_test(asm_file, expected_file)
                
                if result is True:
                    passed += 1
                elif result is False:
                    failed += 1
                else:  # None = skipped
                    skipped += 1
    
    # Run failure tests
    if FAIL_DIR.exists():
        fail_files = sorted(FAIL_DIR.glob('*.asm'))
        
        if fail_files:
            print(f"\n=== Failure Tests ({FAIL_DIR}) ===")
            print(f"Found {len(fail_files)} test files\n")
            
            for asm_file in fail_files:
                result = run_fail_test(asm_file)
                
                if result is True:
                    passed += 1
                elif result is False:
                    failed += 1
    
    # Summary
    print(f"\n{'='*50}")
    print(f"Total: {passed} passed, {failed} failed, {skipped} skipped")
    print(f"{'='*50}")
    
    if skipped > 0:
        print(f"\nNote: Run './regenerate_expected.sh' to generate missing .expected files")
    
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
