# SDDL2 Virtual Machine

Stack-based bytecode interpreter for parsing and decomposing structured data.

## Source of Truth

**`sddl2_opcodes.def`** is the single source of truth for the instruction set.

Several files are auto-generated from this definition and **must not be manually edited**:

- `sddl2_opcodes.h` - C opcode definitions
- `sddl2_disasm_generated.h` - Disassembler implementation
- `tools/sddl/assembler/opcodes_generated.py` - Python assembler definitions

Each generated file contains a header clearly marking it as auto-generated.

### Regenerating Files

After modifying `sddl2_opcodes.def`:

```bash
# Regenerate C headers (VM)
python3 src/openzl/compress/graphs/sddl2/generate_c_headers.py

# Regenerate Python code (assembler)
python3 tools/sddl/assembler/generate_opcodes.py
```

## Debug Traces

The SDDL2 VM uses the OpenZL trace system. Enable traces at build time:

```bash
make BUILD_TYPE=TRACES
# or without ASAN overhead:
make BUILD_TYPE=TRACES_NOSAN
```

For instruction-level traces, add `LOG_LEVEL=POS`:

```bash
make BUILD_TYPE=TRACES_NOSAN LOG_LEVEL=POS
```

Equivalently, using environment variables:

```bash
export BUILD_TYPE=TRACES_NOSAN
export LOG_LEVEL=POS
make
```

### Trace Output Format

Each executed instruction produces one trace line:

```
[SDDL2] @0004: math.add (00020001) | stack depth: 1
```

- `@0004` - Program counter (instruction offset in 32-bit words)
- `math.add` - Instruction mnemonic
- `00020001` - Raw 32-bit instruction word (little-endian hex)
- `stack depth: 1` - Stack depth after execution

### Instruction Encoding

32-bit instruction word format:
```
Bits 15-0  (low):  Opcode within family
Bits 31-16 (high): Family ID
```

Example: `00020001` = Family 0x0002 (MATH), Opcode 0x0001 (add)

See `sddl2_opcodes.def` for complete family/opcode mappings.

## Testing

Run SDDL2-specific tests:

```bash
make sddl2_test
```

These tests are fast and useful during VM development. They're also included in `make test`.

## Related Documentation

- Assembler: `tools/sddl/assembler/README.md`
- Bytecode format: `tools/sddl/assembler/Bytecode_spec.md`
- Test framework: `tests/compress/graphs/sddl2/BYTECODE_TEST_FRAMEWORK.md`
