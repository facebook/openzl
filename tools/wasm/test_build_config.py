# Copyright (c) Meta Platforms, Inc. and affiliates.

import ast
import importlib.resources
import re
import unittest
from pathlib import Path


class BuildConfigTest(unittest.TestCase):
    def setUp(self) -> None:
        # Support both Buck's packaged resources and direct Python execution.
        root = (
            importlib.resources.files(__package__)
            if __package__
            else Path(__file__).parent
        )
        self.wasm_flags = root.joinpath("wasm_flags.bzl").read_text()
        self.cmake = re.sub(r"#[^\n]*", "", root.joinpath("CMakeLists.txt").read_text())
        self.header = root.joinpath("src/openzl_wasm.h").read_text()
        assignment = next(
            node
            for node in ast.parse(self.wasm_flags).body
            if isinstance(node, ast.Assign)
            and any(
                isinstance(target, ast.Name) and target.id == "OPENZL_WASM_LINKER_FLAGS"
                for target in node.targets
            )
        )
        self.buck_flags = ast.literal_eval(assignment.value)
        self.pool_size = re.search(
            r"set\(OPENZL_WASM_PTHREAD_POOL_SIZE\s+(\d+)\s+CACHE", self.cmake
        )
        self.assertIsNotNone(self.pool_size, "CMake worker pool default is missing")

    def test_module_settings_match(self) -> None:
        # Both builds must expose the same module settings to the JS wrapper.
        cmake_options = re.search(
            r"target_link_options\(openzl_wasm PRIVATE\s+(.*?)\n\)",
            self.cmake,
            re.DOTALL,
        )
        self.assertIsNotNone(cmake_options, "CMake module linker options are missing")
        cmake_flags = cmake_options[1].replace(
            "${OPENZL_WASM_PTHREAD_POOL_SIZE}", self.pool_size[1]
        )
        # Excluding three flags as these are set -sWASM_BIGINT and -fexceptions is set globally
        # -W1 and --stack-first is also required by Buck linker
        buck_flags = [
            flag
            for flag in self.buck_flags
            if flag not in ("-sWASM_BIGINT=1", "-fexceptions", "-Wl,--stack-first")
        ]
        # Ignore -sWASM as Buck already produce wasm by default
        cmake_flags = [flag for flag in cmake_flags.split() if flag != "-sWASM=1"]
        self.assertCountEqual(
            buck_flags,
            cmake_flags,
            "Buck and CMake module linker flags have drifted",
        )

    def test_trainer_limit_matches_worker_pool(self) -> None:
        # Test to verify trainer limit is aligned with the worker pool to avoid deadlocks.
        trainer_limit = re.search(
            r"#\s*define OPENZL_WASM_TRAIN_MAX_THREADS\s+(\d+)", self.header
        )
        self.assertIsNotNone(trainer_limit, "Default trainer thread limit is missing")
        self.assertEqual(
            trainer_limit[1],
            self.pool_size[1],
            "Default trainer thread limit must match the startup worker pool",
        )
        self.assertIn(
            "OPENZL_WASM_TRAIN_MAX_THREADS=${OPENZL_WASM_PTHREAD_POOL_SIZE}",
            self.cmake,
        )
