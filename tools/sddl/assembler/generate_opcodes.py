#!/usr/bin/env python3
"""
Generate Python opcode definitions from openzl_opcodes.def

This script parses the C11 X-macro definition file and generates
opcodes_generated.py for use by the assembler.

Usage:
    python3 generate_opcodes.py
"""

import re
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Tuple


def parse_def_file(def_file_path: Path) -> Tuple[Dict[str, int], List[tuple]]:
    """
    Parse the .def file and extract family and opcode definitions.

    Returns:
        (families_dict, opcodes_list)
        families_dict: {name: id}
        opcodes_list: [(mnemonic, family, opcode, [param_types])]
    """
    families = {}
    opcodes = []

    content = def_file_path.read_text()

    # Parse FAMILY_DEF(name, id)
    family_pattern = r"FAMILY_DEF\s*\(\s*(\w+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\)"
    for match in re.finditer(family_pattern, content):
        name = match.group(1)
        id_hex = match.group(2)
        families[name] = int(id_hex, 16)

    # Parse OPCODE_DEF(mnemonic, family, opcode [, params...])
    # Handle variadic parameters - everything after first 3 args
    opcode_pattern = (
        r"OPCODE_DEF\s*\(\s*([^,\)]+)\s*,\s*(\w+)\s*,\s*(0x[0-9A-Fa-f]+)\s*([^\)]*)\)"
    )

    for match in re.finditer(opcode_pattern, content):
        mnemonic_raw = match.group(1).strip()
        family = match.group(2).strip()
        opcode_hex = match.group(3).strip()
        params_raw = match.group(4).strip()

        # Clean mnemonic (remove quotes if present)
        mnemonic = mnemonic_raw.strip('"').strip("'")

        # Parse parameters (variadic part)
        param_types = []
        if params_raw:
            # Split by comma and clean each param
            params = [p.strip() for p in params_raw.split(",") if p.strip()]
            param_types = params

        opcode_value = int(opcode_hex, 16)
        opcodes.append((mnemonic, family, opcode_value, param_types))

    return families, opcodes


def generate_python_code(families: Dict[str, int], opcodes: List[tuple]) -> str:
    """
    Generate Python code for opcodes_generated.py
    """
    lines = []

    # Header
    lines.append('"""')
    lines.append("AUTO-GENERATED FILE - DO NOT EDIT MANUALLY")
    lines.append("")
    lines.append("Generated from: openzl_opcodes.def")
    lines.append(f'Generated at: {datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S UTC")}')
    lines.append("Generator: generate_opcodes.py")
    lines.append("")
    lines.append("To regenerate: python3 generate_opcodes.py")
    lines.append('"""')
    lines.append("")

    # Family definitions
    lines.append("# Family identifiers")
    lines.append("FAMILIES = {")
    for name, id_val in sorted(families.items(), key=lambda x: x[1]):
        lines.append(f'    "{name}": 0x{id_val:04X},')
    lines.append("}")
    lines.append("")

    # Instruction definitions
    lines.append("# Instruction definitions")
    lines.append('# Format: "mnemonic": (family_name, opcode, [param_types])')
    lines.append("INSTRUCTIONS = {")

    # Group by family for readability
    by_family = {}
    for mnemonic, family, opcode, params in opcodes:
        if family not in by_family:
            by_family[family] = []
        by_family[family].append((mnemonic, opcode, params))

    # Output in family order
    family_order = [
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
    ]

    for family_name in family_order:
        if family_name not in by_family:
            continue

        lines.append(f"    # {family_name} family (0x{families[family_name]:04X})")

        for mnemonic, opcode, params in sorted(
            by_family[family_name], key=lambda x: x[1]
        ):
            # Convert params to Python list
            if params:
                params_str = "[" + ", ".join(f'"{p}"' for p in params) + "]"
            else:
                params_str = "[]"

            lines.append(
                f'    "{mnemonic}": ("{family_name}", 0x{opcode:04X}, {params_str}),'
            )

        lines.append("")

    lines.append("}")
    lines.append("")

    return "\n".join(lines)


def main():
    script_dir = Path(__file__).parent
    def_file = script_dir / "openzl_opcodes.def"
    output_file = script_dir / "opcodes_generated.py"

    if not def_file.exists():
        print(f"Error: {def_file} not found")
        return 1

    print(f"Parsing {def_file}...")
    families, opcodes = parse_def_file(def_file)

    print(f"Found {len(families)} families, {len(opcodes)} opcodes")

    print(f"Generating {output_file}...")
    python_code = generate_python_code(families, opcodes)
    output_file.write_text(python_code)

    print(f"Successfully generated {output_file}")
    print(f"  - {len(families)} families")
    print(f"  - {len(opcodes)} instructions")

    return 0


if __name__ == "__main__":
    exit(main())
