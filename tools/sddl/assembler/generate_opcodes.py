#!/usr/bin/env python3
"""
Generate Python opcode definitions from sddl2_opcodes.def

This script parses the structured opcode definition file and generates:
- opcodes_generated.py (Python assembler)

For C header generation, see: src/openzl/compress/graphs/sddl2/generate_c_headers.py

Usage:
    python3 generate_opcodes.py
"""

import re
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Tuple, Optional


def parse_def_file(def_file_path: Path) -> Tuple[Dict[str, tuple], List[tuple]]:
    """
    Parse the .def file and extract family and opcode definitions.

    Format:
        @family NAME ID "Description"
          mnemonic  opcode  [params]  "Description"

    Returns:
        (families_dict, opcodes_list)
        families_dict: {name: (id, description)}
        opcodes_list: [(mnemonic, family, opcode, [param_types], description)]
    """
    families = {}
    opcodes = []

    content = def_file_path.read_text()
    lines = content.split("\n")

    current_family = None

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
                families[family_name] = (int(family_id, 16), description)
                current_family = family_name
            else:
                # Family without description
                match = re.match(r"@family\s+(\w+)\s+(0x[0-9A-Fa-f]+)", stripped)
                if match:
                    family_name = match.group(1)
                    family_id = match.group(2)
                    families[family_name] = (int(family_id, 16), "")
                    current_family = family_name

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
                    params_str = match.group(4)
                    description = match.group(5)

                    # Parse parameters if present
                    params = []
                    if params_str:
                        params = [p.strip() for p in params_str.split() if p.strip()]

                    opcodes.append(
                        (mnemonic, current_family, int(opcode, 16), params, description)
                    )
            elif match:
                mnemonic = match.group(1)
                opcode = match.group(2)
                params_str = match.group(3)
                description = match.group(4)

                # Parse parameters if present
                params = []
                if params_str:
                    params = [p.strip() for p in params_str.split() if p.strip()]

                opcodes.append(
                    (mnemonic, current_family, int(opcode, 16), params, description)
                )

    return families, opcodes


def generate_python_code(families: Dict[str, tuple], opcodes: List[tuple]) -> str:
    """
    Generate Python code for opcodes_generated.py

    Args:
        families: {name: (id, description)}
        opcodes: [(mnemonic, family, opcode, [param_types], description)]
    """
    lines = []

    # Header
    lines.append('"""')
    lines.append("AUTO-GENERATED FILE - DO NOT EDIT MANUALLY")
    lines.append("")
    lines.append("Generated from: src/openzl/compress/graphs/sddl2/sddl2_opcodes.def")
    lines.append(f'Generated at: {datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S UTC")}')
    lines.append("Generator: generate_opcodes.py")
    lines.append("")
    lines.append("To regenerate: python3 generate_opcodes.py")
    lines.append('"""')
    lines.append("")

    # Family definitions
    lines.append("# Family identifiers")
    lines.append("FAMILIES = {")
    for name, (id_val, description) in sorted(families.items(), key=lambda x: x[1][0]):
        lines.append(f'    "{name}": 0x{id_val:04X},')
    lines.append("}")
    lines.append("")

    # Instruction definitions
    lines.append("# Instruction definitions")
    lines.append('# Format: "mnemonic": (family_name, opcode, [param_types])')
    lines.append("INSTRUCTIONS = {")

    # Group by family for readability
    by_family = {}
    for mnemonic, family, opcode, params, description in opcodes:
        if family not in by_family:
            by_family[family] = []
        by_family[family].append((mnemonic, opcode, params, description))

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
        "SEGMENT",
    ]

    for family_name in family_order:
        if family_name not in by_family:
            continue

        id_val, description = families[family_name]
        lines.append(f"    # {family_name} family (0x{id_val:04X})")

        for mnemonic, opcode, params, desc in sorted(
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
    python_output = script_dir / "opcodes_generated.py"

    if not def_file.exists():
        print(f"Error: {def_file} not found")
        return 1

    print(f"Parsing {def_file}...")
    families, opcodes = parse_def_file(def_file)

    print(f"Found {len(families)} families, {len(opcodes)} opcodes")

    # Generate Python code
    print(f"Generating {python_output}...")
    python_code = generate_python_code(families, opcodes)
    python_output.write_text(python_code)
    print(f"  ✓ {python_output}")

    print(f"\nSuccessfully generated Python opcode file:")
    print(f"  - {len(families)} families")
    print(f"  - {len(opcodes)} instructions")
    print(f"\nSingle source of truth: {def_file}")
    print(f"\nFor C header generation, run:")
    print(f"  python3 src/openzl/compress/graphs/sddl2/generate_c_headers.py")

    return 0


if __name__ == "__main__":
    exit(main())
