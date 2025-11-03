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
ASSEMBLER_PATH = SCRIPT_DIR.parent / 'assembler.py'
SUCCESS_DIR = SCRIPT_DIR / 'success'


def run_test(asm_file: Path, expected_file: Path):
    """Run a single test case"""
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


def main():
    if not ASSEMBLER_PATH.exists():
        print(f"Error: Assembler not found at {ASSEMBLER_PATH}")
        return 1
    
    if not SUCCESS_DIR.exists():
        print(f"Error: Success test directory not found at {SUCCESS_DIR}")
        return 1
    
    # Find all .asm files
    asm_files = sorted(SUCCESS_DIR.glob('*.asm'))
    
    if not asm_files:
        print(f"Error: No .asm files found in {SUCCESS_DIR}")
        return 1
    
    print(f"Running tests from {SUCCESS_DIR}")
    print(f"Found {len(asm_files)} test files\n")
    
    passed = 0
    failed = 0
    skipped = 0
    
    for asm_file in asm_files:
        expected_file = asm_file.with_suffix('.expected')
        result = run_test(asm_file, expected_file)
        
        if result is True:
            passed += 1
        elif result is False:
            failed += 1
        else:  # None = skipped
            skipped += 1
    
    # Summary
    print(f"\n{'='*50}")
    print(f"Total: {passed} passed, {failed} failed, {skipped} skipped")
    print(f"{'='*50}")
    
    if skipped > 0:
        print(f"\nNote: Run './regenerate_expected.sh' to generate missing .expected files")
    
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
