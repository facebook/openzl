#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.

import os
import re
from pathlib import Path

OPENZL_DIR = Path(__file__).parents[1]


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


def fixup(file: Path) -> None:
    with open(file, "r") as f:
        content = f.read()

    relpath = file.relative_to(OPENZL_DIR).parent

    regex = re.compile(r"load\(\"//data_compression/experimental/zstrong(/[^:]*:|:)")
    content = regex.sub(lambda match: fixup_match(relpath, match, True), content)

    regex = re.compile(r"//data_compression/experimental/zstrong((/[^:]*:|:))")
    content = regex.sub(lambda match: fixup_match(relpath, match), content)

    with open(file, "w") as f:
        f.write(content)


for root, _, files in os.walk(OPENZL_DIR):
    for file in files:
        if file == "BUCK" or file.endswith(".bzl"):
            fixup(Path(root) / file)
