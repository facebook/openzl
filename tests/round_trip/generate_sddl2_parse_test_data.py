#!/usr/bin/env python3
"""
Generate C++ header file with bytecode constants for SDDL2 parse round-trip tests.

Usage:
    python3 tests/round_trip/generate_sddl2_parse_test_data.py

This script:
1. Finds all .asm files in tests/round_trip/test_data/
2. Uses the SDDL2 assembler to convert each .asm to bytecode
3. Generates a C++ header with bytecode embedded as const uint8_t arrays
4. Eliminates the need to load .bin files at runtime (fixing working directory issues)
"""

import argparse
import sys
from pathlib import Path


def load_assembler(assembler_path):
    """Load the assembler module from the specified path."""
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
    """Convert filename to valid C++ identifier.
    
    Example: 'single_u8_segment.asm' -> 'SINGLE_U8_SEGMENT'
    """
    name = Path(filename).stem  # Remove .asm extension
    return name.upper()


def bytes_to_cpp_array(bytecode):
    """Convert bytecode bytes to C++ array format.
    
    Example: b'\x03\x00\x01\x00' -> "0x03, 0x00, 0x01, 0x00"
    """
    return ', '.join(f'0x{b:02X}' for b in bytecode)


def generate_header(asm_files, output_path, assemble):
    """Generate C++ header file with bytecode constants.
    
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
        "// Generated from SDDL2 parse test assembly files (.asm)",
        "// Source: tests/round_trip/test_data/",
        "// Generator: tests/round_trip/generate_sddl2_parse_test_data.py",
        "//",
        "// To regenerate:",
        "//   python3 tests/round_trip/generate_sddl2_parse_test_data.py",
        "",
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <cstddef>",
        "#include <vector>",
        "",
        "namespace sddl2_parse_test_data {",
        "",
        "/* Bytecode constants from SDDL2 parse test assembly files */",
        ""
    ]
    
    # Generate constants for each .asm file
    for asm_file in sorted(asm_files):
        asm_path = Path(asm_file)
        name = sanitize_name(asm_path.name)
        
        print(f"  Processing: {asm_path.name} -> {name}")
        
        try:
            # Read and assemble the file
            with open(asm_path, 'r') as f:
                source = f.read()
            
            # Assemble to bytecode
            bytecode = assemble(source)
            
            # Generate C++ array with source comment
            lines.append(f"// Source: {asm_path.name}")
            lines.append(f"inline const std::vector<uint8_t> {name} = {{")
            lines.append(f"    {bytes_to_cpp_array(bytecode)}")
            lines.append("};")
            lines.append("")
            
        except Exception as e:
            print(f"  ERROR: Failed to assemble {asm_path.name}: {e}")
            import traceback
            traceback.print_exc()
            continue
    
    # Footer
    lines.extend([
        "} // namespace sddl2_parse_test_data",
        ""
    ])
    
    # Write to file
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    with open(output_path, 'w') as f:
        f.write('\n'.join(lines))
    
    print(f"\n✅ Generated: {output_path}")
    print(f"   Contains {len(asm_files)} bytecode constants")


def main():
    # Get script directory for default paths
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent.parent
    default_input = script_dir / 'test_data'
    default_output = script_dir / 'sddl2_parse_test_data.h'
    default_assembler = project_root / 'tools' / 'sddl' / 'assembler'
    
    parser = argparse.ArgumentParser(
        description='Generate C++ header with bytecode for SDDL2 parse tests',
        epilog='With no arguments, uses default paths relative to script location.')
    parser.add_argument('-i', '--input', 
                        default=str(default_input),
                        help=f'Input directory containing .asm files (default: %(default)s)')
    parser.add_argument('-o', '--output',
                        default=str(default_output),
                        help=f'Output C++ header file path (default: %(default)s)')
    parser.add_argument('-a', '--assembler',
                        default=str(default_assembler),
                        help=f'Path to sddl2_assembler.py or directory containing it (default: %(default)s)')
    
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
