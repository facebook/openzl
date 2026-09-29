#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.


import os
import shutil
import sys
from pathlib import Path
from subprocess import check_call


EXCLUDE = [
    ".git",
]


def main() -> int:
    if len(sys.argv) != 2:
        print("USAGE: import_mkdocstrings_zstd.py <src>", file=sys.stderr)
        return 1
    dst = Path(__file__).parent / "mkdocstrings-zstd"
    src = Path(sys.argv[1])

    buck_path = dst / "src" / "BUCK"
    with open(buck_path, "r") as f:
        buck = f.read()
    shutil.rmtree(dst, ignore_errors=True)

    os.makedirs(dst)

    for name in os.listdir(src):
        if name in EXCLUDE:
            continue
        src_path = src / name
        dst_path = dst / name
        check_call(["cp", "-r", str(src_path), str(dst_path)])

    with open(buck_path, "w") as f:
        f.write(buck)

    check_call(["arc", "lint", "-a"])

    return 0


if __name__ == "__main__":
    sys.exit(main())
