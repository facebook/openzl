#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.

import argparse
import difflib
import os
import re
import subprocess
import sys
from pathlib import Path


def get_openzl_dir() -> Path:
    """Get the openzl/dev directory by calculating from hg root."""
    result = subprocess.run(
        ["hg", "root"],
        capture_output=True,
        text=True,
        check=True,
    )
    hg_root = Path(result.stdout.strip())
    return hg_root / "fbcode" / "openzl" / "dev"


OPENZL_DIR = get_openzl_dir()


def fixup_match(relpath: Path, match: re.Match[str], is_load: bool = False) -> str:
    relrule = match.group(1)
    assert relrule.endswith(":")
    if relrule[0] == "/":
        relrule = relrule[1:]

    relrule = Path(relrule[:-1])

    num_shared = 0
    while (
        num_shared < len(relpath.parts)
        and num_shared < len(relrule.parts)
        and relpath.parts[num_shared] == relrule.parts[num_shared]
    ):
        num_shared += 1

    num_parents = len(relpath.parts) - num_shared

    prefix = Path(*([".."] * num_parents))
    suffix = Path(*relrule.parts[num_shared:])

    path = str(prefix / suffix)

    if path.endswith("/"):
        path = path[:-1]

    if path == ".":
        path = ""

    if is_load:
        assert not path.endswith("/")
        path = f'load("{path}/'
    else:
        path += ":"

    return path


def fixup(file: Path, diff_mode: bool = False) -> bool:
    """
    Fix up Buck dependencies in a file.

    Args:
        file: Path to the file to fix
        diff_mode: If True, print diff instead of writing changes

    Returns:
        True if changes were made (or would be made in diff mode), False otherwise
    """
    with open(file, "r") as f:
        original_content = f.read()

    relpath = file.relative_to(OPENZL_DIR).parent

    content = original_content
    regex = re.compile(r"load\(\"//openzl/dev(/[^:]*:|:)")
    content = regex.sub(lambda match: fixup_match(relpath, match, True), content)

    regex = re.compile(r"//openzl/dev((/[^:]*:|:))")
    content = regex.sub(lambda match: fixup_match(relpath, match), content)

    if content == original_content:
        return False

    if diff_mode:
        # Generate and print unified diff
        diff = difflib.unified_diff(
            original_content.splitlines(keepends=True),
            content.splitlines(keepends=True),
            fromfile=str(file),
            tofile=str(file),
        )
        sys.stdout.writelines(diff)
    else:
        # Write the changes
        with open(file, "w") as f:
            f.write(content)

    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert absolute Buck dependencies in openzl/dev to relative paths"
    )
    parser.add_argument(
        "--diff",
        action="store_true",
        help="Print diffs instead of modifying files. Exit with code 1 if changes would be made.",
    )
    args = parser.parse_args()

    changes_found = False

    for root, _, files in os.walk(OPENZL_DIR):
        for file in files:
            if file == "BUCK" or file.endswith(".bzl"):
                file_path = Path(root) / file
                if fixup(file_path, diff_mode=args.diff):
                    changes_found = True

    if args.diff and changes_found:
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
