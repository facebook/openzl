# SDDL2 Assembler

Assembles SDDL2 assembly language to OpenZL VM bytecode.

> **Note for compiler writers**: If you're implementing a compiler that generates bytecode programmatically (not using this assembler), see `../../../src/openzl/compress/graphs/sddl2/COMPILER_INTEGRATION.md` for integration documentation.

---

## Current Support (Phase 1)

- `halt` - Normal program termination
- `push.zero` - Push constant zero (I64(0)) onto stack

## Usage

**Assemble from file:**
```bash
python3 sddl2_assembler.py input.asm output.bin
```

**Assemble from command line:**
```bash
python3 sddl2_assembler.py -c "push.zero halt"
# Output: 01 00 01 00 05 00 01 00
```

**Save command-line output to file:**
```bash
python3 sddl2_assembler.py -c "push.zero halt" output.bin
```

## Assembly Syntax

- One instruction per line (or separated by whitespace)
- Comments start with `#` or `;` and continue to end of line
- Instruction names are case-sensitive
- Extra whitespace is ignored

## Examples

**Example 1: Empty program**
```asm
# This is valid - implicit halt
```
Bytecode: (empty file)

**Example 2: Explicit halt**
```asm
halt
```
Bytecode: `05 00 01 00`

**Example 3: Push and halt**
```asm
push.zero
halt
```
Bytecode: `01 00 01 00 05 00 01 00`

**Example 4: Multiple instructions**
```asm
# Push three zeros onto stack
push.zero
push.zero
push.zero
halt
```
Bytecode: `01 00 01 00 01 00 01 00 01 00 01 00 05 00 01 00`

## Testing

Run the test suite:
```bash
python3 test_assembler.py
```

## Bytecode Format

All instructions are 32-bit words in little-endian format:

```
[Family_Lo, Family_Hi, Opcode_Lo, Opcode_Hi]
```

- `halt`: Family=0x0005, Opcode=0x0001 → `05 00 01 00`
- `push.zero`: Family=0x0001, Opcode=0x0001 → `01 00 01 00`

**For complete bytecode specification**, see `Bytecode_spec.md`.

## File Structure

- `sddl2_assembler.py` - Main assembler implementation
- `opcodes_generated.py` - Auto-generated opcode definitions (from `sddl2_opcodes.def`)
- `test_assembler.py` - Test suite
- `Bytecode_spec.md` - Complete bytecode format specification
- `README.md` - This file
