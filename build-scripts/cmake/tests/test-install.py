#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.


import shutil
import tempfile
from pathlib import Path
from subprocess import run

# Tests that we can build and install OpenZL, and then use it to build a simple program.
# This ensures that the installed product works, and e.g. the public headers don't depend
# on internal headers.


TEST_DIR = Path(__file__).parent
ROOT_DIR = Path(__file__).parents[3]

BUILD_DIR = tempfile.mkdtemp(dir=TEST_DIR, prefix="build-")
INSTALL_DIR = Path(BUILD_DIR) / "install"

run(
    ["cmake", ROOT_DIR, f"-DCMAKE_INSTALL_PREFIX={INSTALL_DIR}"],
    cwd=BUILD_DIR,
    check=True,
)
run(["make", "install"], cwd=BUILD_DIR, check=True)
run(
    [
        "g++",
        "-std=c++17",
        "main.cpp",
        f"-I{INSTALL_DIR}/include",
        f"-L{INSTALL_DIR}/lib",
        f"-L{INSTALL_DIR}/lib64",
        "-lopenzl_cpp",
        "-lopenzl",
        "-lzstd",
        "-o",
        f"{BUILD_DIR}/main",
    ],
    cwd=TEST_DIR,
    check=True,
)
run(["./main"], cwd=BUILD_DIR, check=True)

# Clean up build dir on success. On failure, keep it around so we can debug.
shutil.rmtree(BUILD_DIR, ignore_errors=True)
