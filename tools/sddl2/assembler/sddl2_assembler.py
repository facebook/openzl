#!/usr/bin/env python3
"""
SDDL2 Assembler

Assembles SDDL2 assembly language to SDDL2 VM bytecode.

Instruction definitions are auto-generated from sddl2_opcodes.def
To regenerate: python3 generate_opcodes.py
"""

import os
import struct
import sys
from typing import List, Tuple

# Import generated opcode definitions
from opcodes_generated import FAMILIES, INSTRUCTIONS


class AssemblerError(Exception):
    """Raised when assembly fails"""

    pass


def parse_integer(token: str) -> int:
    """
    Parse integer literal from various formats.

    OpenZL specification defines three formats:
    - Decimal: 42, -100
    - Hexadecimal: 0x2A, 0xFF (case-insensitive)
    - Binary: 0b101010 (case-insensitive)

    Implementation: Python's int(token, 0) auto-detects these formats.
    Note: This implementation also accepts octal (0o prefix), but this is
    undefined behavior - not part of the spec, and may not work with other
    assemblers.
    """
    try:
        return int(token, 0)
    except ValueError:
        raise AssemblerError(f"Invalid integer literal: {token}")


def validate_range(value: int, param_type: str) -> None:
    """
    Validate that value fits in the specified parameter type.
    """
    ranges = {
        "u32": (0, 4294967295),
        "i32": (-2147483648, 2147483647),
        "i64": (-9223372036854775808, 9223372036854775807),
    }

    if param_type not in ranges:
        raise AssemblerError(f"Unknown parameter type: {param_type}")

    min_val, max_val = ranges[param_type]
    if not (min_val <= value <= max_val):
        raise AssemblerError(
            f"Value {value} out of range for {param_type} "
            f"(must be between {min_val} and {max_val})"
        )


def encode_parameter(value: int, param_type: str) -> bytes:
    """
    Encode parameter value to bytes.
    """
    validate_range(value, param_type)

    if param_type == "u32":
        # Unsigned 32-bit, little-endian
        return struct.pack("<I", value)
    elif param_type == "i32":
        # Signed 32-bit, little-endian
        return struct.pack("<i", value)
    elif param_type == "i64":
        # Signed 64-bit, little-endian
        return struct.pack("<q", value)
    else:
        raise AssemblerError(f"Unknown parameter type: {param_type}")


def tokenize(source: str) -> List[Tuple[str, List[str]]]:
    """
    Tokenize assembly source into (mnemonic, parameters) tuples.
    - Strip comments (# or ; to end of line)
    - Parse instructions and their parameters
    - Handle multiple instructions per line
    - Return list of (mnemonic, [param1, param2, ...])
    """
    lines = source.split("\n")
    all_tokens = []

    # First pass: extract all tokens, removing comments
    for line in lines:
        # Remove comments (support both # and ; for compatibility)
        for comment_char in ["#", ";"]:
            if comment_char in line:
                line = line[: line.index(comment_char)]
                break  # Only need to remove first comment marker

        # Split on whitespace and collect non-empty tokens
        all_tokens.extend(line.split())

    # Second pass: group tokens into (instruction, params)
    instructions = []
    i = 0

    while i < len(all_tokens):
        token = all_tokens[i]

        # Check if this token is a known instruction
        if token in INSTRUCTIONS:
            mnemonic = token
            _, _, param_types = INSTRUCTIONS[mnemonic]

            # Collect the required number of parameters
            params = []
            for j in range(len(param_types)):
                i += 1
                if i >= len(all_tokens):
                    raise AssemblerError(
                        f"Instruction '{mnemonic}' requires {len(param_types)} parameter(s), "
                        f"but only {j} provided"
                    )
                # Make sure the next token isn't another instruction
                if all_tokens[i] in INSTRUCTIONS:
                    raise AssemblerError(
                        f"Instruction '{mnemonic}' requires {len(param_types)} parameter(s), "
                        f"but got instruction '{all_tokens[i]}' instead"
                    )
                params.append(all_tokens[i])

            instructions.append((mnemonic, params))
            i += 1
        # Check for dot-joinable tokens: if token has dots AND next token exists,
        # try joining with dot (e.g., "push.type" + "u8" -> "push.type.u8")
        # This supports syntactic sugar: "push.type u8" -> "push.type.u8"
        elif "." in token and i + 1 < len(all_tokens):
            next_token = all_tokens[i + 1]
            mnemonic_with_dot = f"{token}.{next_token}"

            if mnemonic_with_dot in INSTRUCTIONS:
                mnemonic = mnemonic_with_dot
                _, _, param_types = INSTRUCTIONS[mnemonic]

                # Advance past both tokens (the prefix and the joined part)
                i += 2

                # Collect any remaining parameters
                params = []
                for j in range(len(param_types)):
                    if i >= len(all_tokens):
                        raise AssemblerError(
                            f"Instruction '{mnemonic}' requires {len(param_types)} parameter(s), "
                            f"but only {j} provided"
                        )
                    params.append(all_tokens[i])
                    i += 1

                instructions.append((mnemonic, params))
            else:
                raise AssemblerError(
                    f"Unknown instruction or unexpected token: {token}"
                )
        else:
            raise AssemblerError(f"Unknown instruction or unexpected token: {token}")

    return instructions


def assemble_instruction(mnemonic: str, params: List[str]) -> bytes:
    """
    Assemble a single instruction to bytecode.
    Returns instruction word + immediate values (if any).
    """
    if mnemonic not in INSTRUCTIONS:
        raise AssemblerError(f"Unknown instruction: {mnemonic}")

    family_name, opcode, param_types = INSTRUCTIONS[mnemonic]
    family_id = FAMILIES[family_name]

    # Validate parameter count
    if len(params) != len(param_types):
        if len(param_types) == 0:
            raise AssemblerError(
                f"Instruction '{mnemonic}' takes no parameters, got {len(params)}"
            )
        else:
            raise AssemblerError(
                f"Instruction '{mnemonic}' requires {len(param_types)} parameter(s), got {len(params)}"
            )

    # Encode instruction word
    # Bits 31-16: Family ID
    # Bits 15-0:  Opcode within family
    # Pack as: (family_id << 16) | opcode
    bytecode = bytearray()
    bytecode.extend(struct.pack("<HH", opcode, family_id))

    # Encode parameters (if any)
    for param_str, param_type in zip(params, param_types):
        value = parse_integer(param_str)
        bytecode.extend(encode_parameter(value, param_type))

    return bytes(bytecode)


def assemble(source: str) -> bytes:
    """
    Assemble complete source to bytecode.
    """
    instructions = tokenize(source)
    bytecode = bytearray()

    for mnemonic, params in instructions:
        instruction_bytes = assemble_instruction(mnemonic, params)
        bytecode.extend(instruction_bytes)

    return bytes(bytecode)


def default_output_path(input_file: str) -> str:
    """
    Choose a safe default output path so we never overwrite the source file.
    If the input has an extension, replace it with .bin; otherwise append .bin.
    """
    root, ext = os.path.splitext(input_file)
    return f"{root or input_file}.bin" if ext else f"{input_file}.bin"


def main():
    # Help flag
    if len(sys.argv) > 1 and sys.argv[1] in ["-h", "--help", "help"]:
        print("SDDL2 Assembler")
        print()
        print("Usage:")
        print("  sddl2_assembler.py <input.sddl> [output.bin]")
        print("  sddl2_assembler.py -c '<assembly code>' [output.bin]")
        print()
        print("Arguments:")
        print("  <input.sddl>    Input assembly file (.sddl)")
        print("  [output.bin]    Output bytecode file (optional)")
        print("                  Default: <input>.bin or stdout for -c")
        print()
        print("Options:")
        print("  -c              Assemble code from command line string")
        print("  -h, --help      Show this help message")
        print()
        print("Examples:")
        print("  sddl2_assembler.py program.sddl")
        print("  sddl2_assembler.py program.sddl output.bin")
        print("  sddl2_assembler.py -c 'push.i32 5\\nhalt'")
        sys.exit(0)

    if len(sys.argv) < 2:
        print("Usage: sddl2_assembler.py <input.sddl> [output.bin]")
        print("   or: sddl2_assembler.py -c '<assembly code>'")
        print("Try 'sddl2_assembler.py --help' for more information.")
        sys.exit(1)

    # Command-line mode: -c "code"
    if sys.argv[1] == "-c":
        if len(sys.argv) < 3:
            print("Error: -c requires assembly code argument")
            sys.exit(1)

        source = sys.argv[2]
        output_file = sys.argv[3] if len(sys.argv) > 3 else None

    # File mode
    else:
        input_file = sys.argv[1]
        output_file = (
            sys.argv[2] if len(sys.argv) > 2 else default_output_path(input_file)
        )

        try:
            with open(input_file, "r") as f:
                source = f.read()
        except FileNotFoundError:
            print(f"Error: File not found: {input_file}")
            sys.exit(1)

    # Assemble
    try:
        bytecode = assemble(source)
    except AssemblerError as e:
        print(f"Assembly error: {e}")
        sys.exit(1)

    # Output
    if output_file:
        with open(output_file, "wb") as f:
            f.write(bytecode)
        print(f"Assembled {len(bytecode)} bytes to {output_file}")
    else:
        # Print hex dump to stdout
        hex_dump = " ".join(f"{b:02x}" for b in bytecode)
        print(hex_dump)


if __name__ == "__main__":
    main()
