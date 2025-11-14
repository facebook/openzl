#!/usr/bin/env python3
"""
Generate C header file with bytecode constants from interpreter test assembly files.

Simple usage (from anywhere):
    python3 tests/compress/graphs/sddl2/generate_test_bytecode.py

This uses default paths:
    - Input:     tests/compress/graphs/sddl2/asm/
    - Output:    tests/compress/graphs/sddl2/generated_test_bytecode.h
    - Assembler: tools/sddl/assembler/

Custom paths (optional):
    python3 tests/compress/graphs/sddl2/generate_test_bytecode.py \
        -i custom/asm/dir \
        -o custom/output.h \
        -a path/to/custom/sddl2_assembler.py

What this script does:
1. Finds all .asm files in the input directory
2. Parses test metadata from assembly comments (expected error codes, descriptions)
3. Uses the assembler to convert each .asm to bytecode
4. Generates a C header with bytecode as const uint8_t arrays
5. Creates a lookup table with test metadata for auto-generation
"""

import argparse
import sys
import os
import re
from pathlib import Path


def load_assembler(assembler_path):
    """Load the assembler module from the specified path.
    
    Args:
        assembler_path: Path to sddl2_assembler.py or directory containing it
    
    Returns:
        The assemble function from the assembler module
    """
    assembler_path = Path(assembler_path)
    
    # If it's a directory, look for sddl2_assembler.py inside
    if assembler_path.is_dir():
        assembler_file = assembler_path / 'sddl2_assembler.py'
    else:
        assembler_file = assembler_path
    
    if not assembler_file.exists():
        raise FileNotFoundError(f"Assembler not found at: {assembler_file}")
    
    # Add assembler directory to Python path
    assembler_dir = assembler_file.parent
    sys.path.insert(0, str(assembler_dir))
    
    # Import the assemble function
    from sddl2_assembler import assemble
    return assemble


def sanitize_name(filename):
    """Convert filename to valid C identifier.
    
    Example: 'math_add_basic.asm' -> 'MATH_ADD_BASIC'
    """
    name = Path(filename).stem  # Remove .asm extension
    return name.upper().replace('-', '_')


def parse_test_metadata(source):
    """Parse test metadata from assembly source comments.
    
    Metadata format:
        # Test: <description>
        # Expected: <SDDL2_ERROR_CODE>
        # Input: <size_in_bytes>
        # Custom: <custom_validator_function>
        # Skip: <true|false>
    
    Args:
        source: Assembly source code string
        
    Returns:
        dict with keys: 'description', 'expected_error', 'input_size', 'custom_validator', 'skip'
    """
    metadata = {
        'description': '',
        'expected_error': 'SDDL2_OK',  # Default: expect success
        'input_size': 0,  # Default: no input needed
        'custom_validator': None,
        'skip': False  # Default: don't skip
    }
    
    # Valid error codes we recognize
    valid_errors = {
        'SDDL2_OK', 'SDDL2_STACK_UNDERFLOW', 'SDDL2_TYPE_MISMATCH',
        'SDDL2_VALIDATION_FAILED', 'SDDL2_DIV_ZERO',
        'SDDL2_INVALID_BYTECODE', 'SDDL2_UNKNOWN_OPCODE',
        'SDDL2_INVALID_TYPE', 'SDDL2_OUT_OF_MEMORY',
        'SDDL2_STACK_OVERFLOW', 'SDDL2_LOAD_BOUNDS',
        'SDDL2_SEGMENT_BOUNDS', 'SDDL2_LIMIT_EXCEEDED',
        'SDDL2_ALLOCATION_FAILED'
    }
    
    lines = source.split('\n')
    for line in lines:
        line = line.strip()
        
        # Parse description
        if line.startswith('# Test:'):
            metadata['description'] = line[7:].strip()
        
        # Parse expected error
        elif line.startswith('# Expected:'):
            expected = line[11:].strip()
            # Only accept if it looks like a valid error code
            if expected and not expected.startswith('#'):
                # Try to extract error code
                error_match = re.match(r'(SDDL2_[A-Z_]+)', expected)
                if error_match:
                    error_code = error_match.group(1)
                    if error_code in valid_errors:
                        metadata['expected_error'] = error_code
                elif expected.upper() in [e.replace('SDDL2_', '') for e in valid_errors]:
                    # Handle "STACK_UNDERFLOW" without "SDDL2_" prefix
                    metadata['expected_error'] = 'SDDL2_' + expected.upper()
        
        # Parse custom validator
        elif line.startswith('# Custom:'):
            metadata['custom_validator'] = line[9:].strip()
        
        # Parse input size requirement
        elif line.startswith('# Input:'):
            input_spec = line[8:].strip()
            # Try to parse as integer
            try:
                metadata['input_size'] = int(input_spec)
            except ValueError:
                # Could also support expressions like ">=5" later
                pass
        
        # Parse skip flag
        elif line.startswith('# Skip:'):
            skip_spec = line[7:].strip().lower()
            metadata['skip'] = skip_spec in ('true', 'yes', '1')
        
        # Stop parsing at first non-comment line
        elif line and not line.startswith('#'):
            break
    
    return metadata


def bytes_to_c_array(bytecode):
    """Convert bytecode bytes to C array format.
    
    Example: b'\x03\x00\x01\x00' -> "0x03, 0x00, 0x01, 0x00"
    """
    return ', '.join(f'0x{b:02X}' for b in bytecode)


def generate_header(asm_files, output_path, assemble):
    """Generate C header file with bytecode constants and test metadata.
    
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
        "// Generator: tests/compress/graphs/sddl2/generate_test_bytecode.py",
        "//",
        "// To regenerate:",
        "//   python3 tests/compress/graphs/sddl2/generate_test_bytecode.py \\",
        "//       -i tests/compress/graphs/sddl2/asm \\",
        "//       -o tests/compress/graphs/sddl2/generated_test_bytecode.h",
        "",
        "#ifndef GENERATED_TEST_BYTECODE_H",
        "#define GENERATED_TEST_BYTECODE_H",
        "",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "#include <string.h>",
        '#include "openzl/compress/graphs/sddl2/sddl2_interpreter.h"',
        "",
        "/* Bytecode constants from interpreter test assembly files */",
        ""
    ]
    
    # Generate constants for each .asm file
    test_metadata = []  # List of (name, original_name, metadata)
    
    for asm_file in sorted(asm_files):
        asm_path = Path(asm_file)
        name = sanitize_name(asm_path.name)
        
        print(f"  Processing: {asm_path.name} -> {name}")
        
        try:
            # Read and assemble the file
            with open(asm_path, 'r') as f:
                source = f.read()
            
            # Parse metadata from comments
            metadata = parse_test_metadata(source)
            test_metadata.append((name, asm_path.stem, metadata))
            
            # Assemble to bytecode
            bytecode = assemble(source)
            
            # Generate C array with metadata in comments
            lines.append(f"/* Source: {asm_path.name} */")
            if metadata['description']:
                lines.append(f"/* {metadata['description']} */")
            if metadata['expected_error'] != 'SDDL2_OK':
                lines.append(f"/* Expected error: {metadata['expected_error']} */")
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
    
    # Generate test metadata table
    lines.extend([
        "/* Test metadata for auto-generated tests */",
        "typedef struct {",
        "    const char* name;",
        "    const uint8_t* bytecode;",
        "    size_t size;",
        "    const char* description;",
        "    SDDL2_Error expected_error;",
        "    size_t input_size;                /* Minimum input size required (0 = none) */",
        "    int skip;                         /* Skip in auto-test (1 = skip, 0 = run) */",
        "    const char* custom_validator;     /* NULL = use default validator */",
        "} SDDL2_TestCase;",
        "",
        "static const SDDL2_TestCase SDDL2_BYTECODE_TESTS[] = {"
    ])
    
    for name, original_name, metadata in test_metadata:
        desc = metadata['description'].replace('"', '\\"')  # Escape quotes
        custom = metadata['custom_validator']
        custom_str = f'"{custom}"' if custom else 'NULL'
        skip_val = '1' if metadata['skip'] else '0'
        
        lines.append(f'    {{')
        lines.append(f'        .name = "{original_name}",')
        lines.append(f'        .bytecode = BYTECODE_{name},')
        lines.append(f'        .size = BYTECODE_{name}_SIZE,')
        lines.append(f'        .description = "{desc}",')
        lines.append(f'        .expected_error = {metadata["expected_error"]},')
        lines.append(f'        .input_size = {metadata["input_size"]},')
        lines.append(f'        .skip = {skip_val},')
        lines.append(f'        .custom_validator = {custom_str}')
        lines.append(f'    }},')
    
    lines.extend([
        "};",
        "",
        f"static const size_t SDDL2_BYTECODE_TEST_COUNT = {len(test_metadata)};",
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
    print(f"   Contains {len(test_metadata)} bytecode constants with metadata")


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
