#!/usr/bin/env python3
"""
Generate C header file with bytecode constants from interpreter test assembly files.

Simple usage (from anywhere):
    python3 tests/compress/graphs/sddlv2/generate_test_bytecode.py

This uses default paths:
    - Input:     tests/compress/graphs/sddlv2/asm/
    - Output:    tests/compress/graphs/sddlv2/generated_test_bytecode.h
    - Assembler: tools/sddl/assembler/

Custom paths (optional):
    python3 tests/compress/graphs/sddlv2/generate_test_bytecode.py \
        -i custom/asm/dir \
        -o custom/output.h \
        -a path/to/custom/assembler.py

What this script does:
1. Finds all .asm files in the input directory
2. Uses the assembler to convert each .asm to bytecode
3. Generates a C header with bytecode as const uint8_t arrays
4. Creates a lookup function for easy access by test name
"""

import argparse
import sys
import os
from pathlib import Path


def load_assembler(assembler_path):
    """Load the assembler module from the specified path.
    
    Args:
        assembler_path: Path to assembler.py or directory containing it
    
    Returns:
        The assemble function from the assembler module
    """
    assembler_path = Path(assembler_path)
    
    # If it's a directory, look for assembler.py inside
    if assembler_path.is_dir():
        assembler_file = assembler_path / 'assembler.py'
    else:
        assembler_file = assembler_path
    
    if not assembler_file.exists():
        raise FileNotFoundError(f"Assembler not found at: {assembler_file}")
    
    # Add assembler directory to Python path
    assembler_dir = assembler_file.parent
    sys.path.insert(0, str(assembler_dir))
    
    # Import the assemble function
    from assembler import assemble
    return assemble


def sanitize_name(filename):
    """Convert filename to valid C identifier.
    
    Example: 'math_add_basic.asm' -> 'MATH_ADD_BASIC'
    """
    name = Path(filename).stem  # Remove .asm extension
    return name.upper().replace('-', '_')


def bytes_to_c_array(bytecode):
    """Convert bytecode bytes to C array format.
    
    Example: b'\x03\x00\x01\x00' -> "0x03, 0x00, 0x01, 0x00"
    """
    return ', '.join(f'0x{b:02X}' for b in bytecode)


def generate_header(asm_files, output_path, assemble):
    """Generate C header file with bytecode constants.
    
    Args:
        asm_files: List of .asm file paths
        output_path: Output header file path
        assemble: Assembler function to convert assembly to bytecode
    """
    
    print(f"Generating bytecode header from {len(asm_files)} .asm files...")
    
    # Header
    lines = [
        "// AUTO-GENERATED FILE - DO NOT EDIT MANUALLY",
        "//",
        "// Generated from interpreter test assembly files (.asm)",
        f"// Source: {Path(asm_files[0]).parent if asm_files else 'N/A'}",
        "// Generator: tests/compress/graphs/sddlv2/generate_test_bytecode.py",
        "//",
        "// To regenerate:",
        "//   python3 tests/compress/graphs/sddlv2/generate_test_bytecode.py \\",
        "//       -i tests/compress/graphs/sddlv2/asm \\",
        "//       -o tests/compress/graphs/sddlv2/generated_test_bytecode.h",
        "",
        "#ifndef GENERATED_TEST_BYTECODE_H",
        "#define GENERATED_TEST_BYTECODE_H",
        "",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "#include <string.h>",
        "",
        "/* Bytecode constants from interpreter test assembly files */",
        ""
    ]
    
    # Generate constants for each .asm file
    test_names = []
    
    for asm_file in sorted(asm_files):
        asm_path = Path(asm_file)
        name = sanitize_name(asm_path.name)
        test_names.append((name, asm_path.stem))
        
        print(f"  Processing: {asm_path.name} -> {name}")
        
        try:
            # Read and assemble the file
            with open(asm_path, 'r') as f:
                source = f.read()
            bytecode = assemble(source)
            
            # Generate C array
            lines.append(f"/* Source: {asm_path.name} */")
            lines.append(f"static const uint8_t BYTECODE_{name}[] = {{")
            lines.append(f"    {bytes_to_c_array(bytecode)}")
            lines.append("};")
            lines.append(f"static const size_t BYTECODE_{name}_SIZE = {len(bytecode)};")
            lines.append("")
            
        except Exception as e:
            print(f"  ERROR: Failed to assemble {asm_path.name}: {e}")
            import traceback
            traceback.print_exc()
            continue
    
    # Generate lookup struct
    lines.extend([
        "/* Bytecode lookup by test name */",
        "typedef struct {",
        "    const char* name;",
        "    const uint8_t* bytecode;",
        "    size_t size;",
        "} TestBytecode;",
        "",
        "static const TestBytecode TEST_BYTECODE_LOOKUP[] = {"
    ])
    
    for name, original_name in test_names:
        lines.append(f'    {{ "{original_name}", BYTECODE_{name}, BYTECODE_{name}_SIZE }},')
    
    lines.extend([
        "};",
        "",
        f"static const size_t TEST_BYTECODE_COUNT = {len(test_names)};",
        "",
        "#endif // GENERATED_TEST_BYTECODE_H",
        ""
    ])
    
    # Write to file
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    with open(output_path, 'w') as f:
        f.write('\n'.join(lines))
    
    print(f"\n✅ Generated: {output_path}")
    print(f"   Contains {len(test_names)} bytecode constants")


def main():
    # Get script directory for default paths
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent.parent.parent.parent
    default_input = script_dir / 'asm'
    default_output = script_dir / 'generated_test_bytecode.h'
    default_assembler = project_root / 'tools' / 'sddl' / 'assembler'
    
    parser = argparse.ArgumentParser(
        description='Generate C header with bytecode from interpreter test assembly files',
        epilog='With no arguments, uses default paths relative to script location.')
    parser.add_argument('-i', '--input', 
                        default=str(default_input),
                        help=f'Input directory containing .asm files (default: %(default)s)')
    parser.add_argument('-o', '--output',
                        default=str(default_output),
                        help=f'Output C header file path (default: %(default)s)')
    parser.add_argument('-a', '--assembler',
                        default=str(default_assembler),
                        help=f'Path to assembler.py or directory containing it (default: %(default)s)')
    
    args = parser.parse_args()
    
    # Load assembler
    try:
        assemble = load_assembler(args.assembler)
    except FileNotFoundError as e:
        print(f"ERROR: {e}")
        return 1
    except Exception as e:
        print(f"ERROR: Failed to load assembler: {e}")
        import traceback
        traceback.print_exc()
        return 1
    
    # Find all .asm files
    input_dir = Path(args.input)
    if not input_dir.is_dir():
        print(f"ERROR: Input directory not found: {input_dir}")
        return 1
    
    asm_files = list(input_dir.glob('*.asm'))
    if not asm_files:
        print(f"ERROR: No .asm files found in {input_dir}")
        return 1
    
    # Generate header
    generate_header(asm_files, args.output, assemble)
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
