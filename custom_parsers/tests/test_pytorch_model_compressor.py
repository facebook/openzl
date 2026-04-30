#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.


import os
import shutil
import subprocess
import sys
import tempfile

import openzl.ext as zl


def main(generator, compressor):
    try:
        tmpdir = tempfile.mkdtemp()
        subprocess.check_call(
            [generator, "PytorchModelParserTest", "TestRoundTrip", tmpdir]
        )
        for file in os.listdir(tmpdir)[:10]:
            for format_version in range(16, zl.MAX_FORMAT_VERSION + 1):
                print(f"Testing format version {format_version} for {file}")
                path = os.path.join(tmpdir, file)
                subprocess.check_call([compressor, str(format_version), path])
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)

    try:
        tmpdir = tempfile.mkdtemp()
        subprocess.check_call(
            [generator, "PytorchModelParserTest", "TestRoundTripLarge", tmpdir]
        )
        file = os.listdir(tmpdir)[0]
        for format_version in range(16, zl.MAX_FORMAT_VERSION + 1):
            print(f"Testing format version {format_version} for {file}")
            path = os.path.join(tmpdir, file)
            subprocess.check_call([compressor, str(format_version), path])
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    main(*sys.argv[1:])
