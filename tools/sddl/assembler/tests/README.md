# Assembler Test Suite

This directory contains the test suite for the OpenZL assembler.

## Test Structure

Tests are organized into directories:

- **`success/`** - Test cases that should assemble successfully
  - Each test consists of two files:
    - `*.asm` - Assembly source code
    - `*.expected` - Expected hexadecimal bytecode output

- **`fail/`** - Test cases that should fail to assemble
  - Each test is a single `*.asm` file
  - Optional `# EXPECT-ERROR: substring` comment specifies expected error message

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

### Success Tests

To add a new success test:

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

### Failure Tests

To add a new failure test:

1. Create a new `.asm` file in `fail/`:
   ```bash
   cat > fail/my_error_test.asm << 'EOF'
   # EXPECT-ERROR: out of range
   push.u32 -1
   halt
   EOF
   ```

2. Run tests to verify:
   ```bash
   python3 test_assembler.py
   ```

**EXPECT-ERROR format:**
- Optional: If omitted, test just verifies assembler fails (non-zero exit code)
- If present: Test verifies error message contains the specified substring (case-insensitive)
- Flexible: Use short, stable substrings that won't break when error messages improve

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

## Legacy Test Files

- `tests_phase2.py` (inline tests, superseded by auto-discovery system)
