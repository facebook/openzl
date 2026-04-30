#!/usr/bin/env python3
"""
Generate CPP opcode definitions from sddl2_opcodes.def

This script parses the structured opcode definition file and generates:
- OpcodesGenerated.h (CPP assembler)

For C header generation, see: src/openzl/compress/graphs/sddl2/generate_c_headers.py

Usage:
    python3 generate_opcodes.py
"""

import re
from datetime import datetime
from enum import Enum
from pathlib import Path
from typing import List, NamedTuple


class Param(Enum):
    U32 = "U32"
    I32 = "I32"
    I64 = "I64"


class OpCode(NamedTuple):
    mnemonic: str
    opcode: int
    params: List[Enum]
    description: str


class Family(NamedTuple):
    name: str
    id: int
    description: str
    opcodes: List[OpCode]


family_order: list[str] = [
    "CONTROL",
    "PUSH",
    "STACK",
    "MATH",
    "CMP",
    "LOGIC",
    "LOAD",
    "TYPE",
    "VAR",
    "EXPECT",
    "CALL",
    "SEGMENT",
]

param_str_to_enum: dict[str, Param] = {
    "u32": Param.U32,
    "i32": Param.I32,
    "i64": Param.I64,
}


def parse_def_file(def_file_path: Path) -> list[Family]:
    """
    Parse the .def file and extract family and opcode definitions.

    Format:
        @family NAME ID "Description"
          mnemonic  opcode  [params]  "Description"

    Returns:
        families: [Family] list of Family descriptions (name, id, description, opcodes)
    """
    current_family = None
    families: list[Family] = []

    content = def_file_path.read_text()
    lines = content.split("\n")

    for line in lines:
        stripped = line.strip()

        # Skip comments and empty lines
        if not stripped or stripped.startswith("#"):
            continue

        # Parse @family directive: @family NAME ID "Description"
        if stripped.startswith("@family"):
            # Extract family name, ID, and description (in quotes)
            match = re.match(
                r'@family\s+(\w+)\s+(0x[0-9A-Fa-f]+)\s+"([^"]*)"', stripped
            )
            if match:
                family_name = match.group(1)
                family_id = match.group(2)
                description = match.group(3)
                current_family = Family(
                    family_name, int(family_id, 16), description, []
                )
                families.append(current_family)
                continue
            else:
                # Family without description
                match = re.match(r"@family\s+(\w+)\s+(0x[0-9A-Fa-f]+)", stripped)
                assert match
                family_name = match.group(1)
                family_id = match.group(2)
                current_family = Family(family_name, int(family_id, 16), "", [])
                families.append(current_family)

        # Parse indented opcode lines
        elif line.startswith("  ") and current_family:
            # Format: mnemonic  opcode  [params]  "Description"
            # Mnemonic is used exactly as written (e.g., "halt", "push.zero")
            # Special case: "push.type <typename>" is treated as a single mnemonic

            # First, try the standard pattern
            match = re.match(
                r'\s+([\w.]+)\s+(0x[0-9A-Fa-f]+)(?:\s+([^"]+))?\s+"([^"]*)"', line
            )

            if not match:
                # Try multi-word mnemonic pattern (for "push.type <typename>")
                match = re.match(
                    r'\s+([\w.]+)\s+([\w.]+)\s+(0x[0-9A-Fa-f]+)(?:\s+([^"]+))?\s+"([^"]*)"',
                    line,
                )
                if match and match.group(1) == "push.type":
                    # Merge "push.type" and the typename
                    mnemonic = f"{match.group(1)} {match.group(2)}"
                    opcode = match.group(3)
                    params_str = match.group(4) or ""
                    description = match.group(5)

                    # Parse parameters if present
                    params = [param_str_to_enum[p.strip()] for p in params_str.split()]

                    current_family.opcodes.append(
                        OpCode(
                            mnemonic,
                            int(opcode, 16),
                            params,
                            description,
                        )
                    )
            elif match:
                mnemonic = match.group(1)
                opcode = match.group(2)
                params_str = match.group(3) or ""
                description = match.group(4)

                # Parse parameters if present
                params = [param_str_to_enum[p.strip()] for p in params_str.split()]

                current_family.opcodes.append(
                    OpCode(mnemonic, int(opcode, 16), params, description)
                )

    return families


def cpp_header_comment() -> list[str]:
    lines = []
    lines.append("// Copyright (c) Meta Platforms, Inc. and affiliates.")
    lines.append("")
    lines.append("// AUTO-GENERATED FILE - DO NOT EDIT MANUALLY")
    lines.append("//")
    lines.append(
        "// Generated from: src/openzl/compress/graphs/sddl2/sddl2_opcodes.def"
    )
    lines.append(
        f"// Generated at: {datetime.utcnow().strftime('%Y-%m-%d %H:%M:%S UTC')}"
    )
    lines.append("// Generator: generate_opcodes.py")
    lines.append("//")
    lines.append("// To regenerate: python3 generate_opcodes.py")

    return lines


def generate_cpp_header_code(families: list[Family]) -> str:
    """
    Generate C++ code for OpcodesGenerated.h

    Args:
        families: List of Family descriptions (name, id, description, opcodes)
    """
    lines = cpp_header_comment()

    lines.append("")
    lines.append("#pragma once")
    lines.append("#include <cstdint>")
    lines.append("#include <string>")
    lines.append("#include <optional>")
    lines.append("#include <vector>")
    lines.append("")
    lines.append("namespace openzl {")
    lines.append("namespace sddl2 {")
    lines.append("namespace opcodes {")

    # Family Enum
    lines.append("")
    lines.append("enum class Family : uint16_t {")
    for family in families:
        lines.append(f"    {family.name} = 0x{family.id:04X},")
    lines.append("};")

    # ParamType Enum
    lines.append("")
    lines.append("enum class ParamType : uint8_t {")
    lines.append("    U32,")
    lines.append("    I32,")
    lines.append("    I64,")
    lines.append("};")

    # Instruction Definition Struct
    lines.append("")
    lines.append("struct Instruction {")
    lines.append("    Family family;")
    lines.append("    uint16_t opcode;")
    lines.append("    std::vector<ParamType> params;")
    lines.append("};")

    # Instruction Map Accessor
    lines.append("")
    lines.append(
        "std::optional<Instruction> getInstruction(const std::string& mnemonic);"
    )

    lines.append("")
    lines.append("} // namespace opcodes")
    lines.append("} // namespace sddl2")
    lines.append("} // namespace openzl")
    return "\n".join(lines)


def generate_cpp_source_code(families: list[Family]) -> str:
    """
    Generate C++ code for OpcodesGenerated.cpp

    Args:
        families: List of Family descriptions (name, id, description, opcodes)
    """
    lines = cpp_header_comment()

    lines.append("")
    lines.append('#include "tools/sddl2/assembler/OpcodesGenerated.h"')
    lines.append("#include <unordered_map>")

    lines.append("namespace openzl {")
    lines.append("namespace sddl2 {")
    lines.append("namespace opcodes {")

    lines.append("namespace {")
    # Instruction Map
    lines.append("")
    lines.append("const std::unordered_map<std::string, Instruction> INSTRUCTIONS = {")
    for family in sorted(families, key=lambda f: family_order.index(f.name)):
        lines.append(f"    // {family.name} family (0x{family.id:04X})")

        for mnemonic, opcode, params, _desc in sorted(
            family.opcodes, key=lambda x: x.opcode
        ):
            # Convert params to C++ vector
            params_str = "std::vector<ParamType> {"
            params_str += ", ".join(f"ParamType::{p.value}" for p in params)
            params_str += "}"

            lines.append(
                f'    {{ "{mnemonic}", Instruction{{Family::{family.name}, 0x{opcode:04X}, {params_str} }}}},'
            )

        lines.append("")
    lines.append("};")
    lines.append("} // namespace")

    # getInstruction() Implementation
    lines.append("")
    lines.append(
        "std::optional<Instruction> getInstruction(const std::string& mnemonic) {"
    )
    lines.append("    auto it = INSTRUCTIONS.find(mnemonic);")
    lines.append("    if (it == INSTRUCTIONS.end()) {")
    lines.append("        return std::nullopt;")
    lines.append("    }")
    lines.append("    return it->second;")
    lines.append("}")

    lines.append("")
    lines.append("} // namespace opcodes")
    lines.append("} // namespace sddl2")
    lines.append("} // namespace openzl")
    return "\n".join(lines)


def main():
    script_dir = Path(__file__).parent
    # Point to the source of truth in src/openzl/
    repo_root = script_dir.parent.parent.parent
    def_file = (
        repo_root
        / "src"
        / "openzl"
        / "compress"
        / "graphs"
        / "sddl2"
        / "sddl2_opcodes.def"
    )
    if not def_file.exists():
        print(f"Error: {def_file} not found")
        return 1

    print(f"Parsing {def_file}...")
    families = parse_def_file(def_file)
    num_families = len(families)
    num_opcodes = sum([len(f.opcodes) for f in families])
    print(f"Found {num_families} families, {num_opcodes} opcodes")

    # Generate CPP Code
    cpp_header: Path = script_dir / "OpcodesGenerated.h"
    print(f"Generating {cpp_header}...")
    cpp_header_code = generate_cpp_header_code(families)
    cpp_header.write_text(cpp_header_code)
    print(f"  ✓ {cpp_header}")

    cpp_source = script_dir / "OpcodesGenerated.cpp"
    print(f"Generating {cpp_source}...")
    cpp_source_code = generate_cpp_source_code(families)
    cpp_source.write_text(cpp_source_code)
    print(f"  ✓ {cpp_source}")

    print("\nSuccessfully generated CPP opcode file:")
    print(f"  - {num_families} families")
    print(f"  - {num_opcodes} instructions")
    print(f"\nSingle source of truth: {def_file}")
    print("\nFor C header generation, run:")
    print("  python3 src/openzl/compress/graphs/sddl2/generate_c_headers.py")

    return 0


if __name__ == "__main__":
    exit(main())
