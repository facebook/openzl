#!/usr/bin/env python3
"""
Generate the SDDL2 assembly opcode reference inside Assembly_opcodes.md.

Source of truth: src/openzl/compress/graphs/sddl2/sddl2_opcodes.def
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Dict, List, Optional


ROOT = Path(__file__).resolve().parents[3]
OPCODES_DEF = (
    ROOT / "src" / "openzl" / "compress" / "graphs" / "sddl2" / "sddl2_opcodes.def"
)
REFERENCE_MD = ROOT / "tools" / "sddl2" / "assembler" / "Assembly_opcodes.md"

BEGIN_MARKER = "<!-- BEGIN SDDL2_OPCODE_LIST -->"
END_MARKER = "<!-- END SDDL2_OPCODE_LIST -->"


def _escape_md(text: str) -> str:
    return text.replace("|", "\\|")


def parse_opcodes(path: Path) -> List[Dict[str, object]]:
    families: List[Dict[str, object]] = []
    current: Optional[Dict[str, object]] = None

    family_re = re.compile(r'^@family\s+(\S+)\s+(0x[0-9A-Fa-f]+)\s+"([^"]*)"\s*$')
    inst_re = re.compile(
        r"^([^\s]+)\s+(0x[0-9A-Fa-f]+)"
        r'(?:\s+([a-z0-9]+))?\s+"([^"]*)"\s*$'
    )

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        family_match = family_re.match(line)
        if family_match:
            current = {
                "name": family_match.group(1),
                "id": family_match.group(2),
                "desc": family_match.group(3),
                "ops": [],
            }
            families.append(current)
            continue

        if current is None:
            continue

        inst_match = inst_re.match(line)
        if not inst_match:
            continue

        current["ops"].append(
            {
                "mnemonic": inst_match.group(1),
                "opcode": inst_match.group(2),
                "params": inst_match.group(3) or "",
                "desc": inst_match.group(4),
            }
        )

    return families


def _pad(text: str, width: int) -> str:
    if len(text) >= width:
        return text
    return text + (" " * (width - len(text)))


def render_appendix(families: List[Dict[str, object]]) -> str:
    lines: List[str] = []
    lines.append(BEGIN_MARKER)
    lines.append(
        "_This section is auto-generated from `src/openzl/compress/graphs/sddl2/sddl2_opcodes.def`._"
    )

    for family in families:
        name = family["name"]
        fam_id = family["id"]
        desc = family["desc"]
        ops = family["ops"]

        lines.append("")
        lines.append(f"### {name} ({fam_id})")
        if desc:
            lines.append(_escape_md(str(desc)))
        lines.append("")

        if not ops:
            lines.append("_No instructions defined._")
            continue

        rows: List[List[str]] = []
        for op in ops:
            mnemonic = f"`{op['mnemonic']}`"
            opcode = f"`{op['opcode']}`"
            params = f"`{op['params'] or '-'}`"
            desc = _escape_md(str(op["desc"]))
            rows.append([mnemonic, opcode, params, desc])

        header = ["Mnemonic", "Opcode", "Params", "Description"]
        widths = [len(h) for h in header]
        for row in rows:
            for idx, cell in enumerate(row):
                if len(cell) > widths[idx]:
                    widths[idx] = len(cell)

        lines.append(
            "| " + " | ".join(_pad(header[i], widths[i]) for i in range(4)) + " |"
        )
        lines.append(
            "| " + " | ".join("-" * max(widths[i], 3) for i in range(4)) + " |"
        )
        for row in rows:
            lines.append(
                "| " + " | ".join(_pad(row[i], widths[i]) for i in range(4)) + " |"
            )

    lines.append("")
    lines.append(END_MARKER)
    return "\n".join(lines)


def update_reference(reference_path: Path, appendix: str) -> None:
    content = reference_path.read_text(encoding="utf-8")
    if BEGIN_MARKER not in content or END_MARKER not in content:
        raise RuntimeError(
            f"Missing markers in {reference_path}. Add {BEGIN_MARKER} / {END_MARKER}."
        )

    pre, _, rest = content.partition(BEGIN_MARKER)
    _, _, post = rest.partition(END_MARKER)
    new_content = f"{pre}{appendix}{post}"
    reference_path.write_text(new_content, encoding="utf-8")


def main() -> None:
    families = parse_opcodes(OPCODES_DEF)
    appendix = render_appendix(families)
    update_reference(REFERENCE_MD, appendix)
    print(f"Updated {REFERENCE_MD}")


if __name__ == "__main__":
    main()
