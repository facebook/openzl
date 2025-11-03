# Assembler Test Suite

This directory contains the test suite for the OpenZL assembler.

## Test Structure

Tests are organized into directories:

- **`success/`** - Test cases that should assemble successfully
  - Each test consists of two files:
    - `*.asm` - Assembly source code
    - `*.expected` - Expected hexadecimal bytecode output

## Running Tests

Run all tests:
```bash
cd tests
python3 test_assembler.py
```

The test runner will:
1. Auto-discover all `.asm` files in `success/`
2. Assemble each file
3. Compare output against corresponding `.expected` file
4. Report pass/fail for each test

## Adding New Tests

To add a new test:

1. Create a new `.asm` file in `success/`:
   ```bash
   echo "push.u32 123" > success/my_new_test.asm
   echo "halt" >> success/my_new_test.asm
   ```

2. Generate the `.expected` file:
   ```bash
   ./regenerate_expected.sh
   ```

3. Run tests to verify:
   ```bash
   python3 test_assembler.py
   ```

## Regenerating Expected Files

When the bytecode format changes, regenerate all `.expected` files at once:

```bash
./regenerate_expected.sh
```

This script:
- Finds all `.asm` files in `success/`
- Runs the assembler on each one
- Saves output to corresponding `.expected` file
- Reports any assembly errors

**Use this when:**
- Bytecode encoding changes
- Instruction format changes
- After fixing bugs that affect output

## Test Coverage

Current test coverage (23 tests):

**Phase 1:**
- empty program
- halt instruction
- push.zero instruction

**Phase 2:**
- push.u32 (0, 42, 255, max, hex, binary formats)
- push.i32 (0, 42, -1, -100, min, max, negative hex)
- push.i64 (0, 1000000, -1, min, max)
- Multiple instructions (mixed types, combinations)

## Legacy Test Files

- `tests_phase2.py` (inline tests, superseded by auto-discovery system)
