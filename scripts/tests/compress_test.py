# Copyright (c) Meta Platforms, Inc. and affiliates.


import importlib.resources
import json
import os
import shutil
import subprocess
import tempfile
import unittest


class CompressTest(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.mkdtemp()
        self.input = os.path.join(self.tempdir, "input")
        self.output = self.input + ".zs"
        self.json = os.path.join(self.tempdir, "graph.json")
        with open(self.input, "wb") as f:
            f.write(b"x" * 65536)

    def tearDown(self):
        shutil.rmtree(self.tempdir, ignore_errors=True)

    def compress(self, args: list[str]):
        with importlib.resources.path(__package__, "compress") as compress:
            subprocess.check_call([compress] + args)

    def dump(self, graph):
        with open(self.json, "w") as f:
            json.dump(graph, f)

    def test_constant(self) -> None:
        """
        Tests a constant graph
        """
        self.assertFalse(os.path.exists(self.output))

        graph = {"name": "constant"}
        self.compress([json.dumps(graph), self.input])

        self.assertTrue(os.path.exists(self.output))

    def test_json_file(self) -> None:
        """
        Tests a store graph in a file
        """
        self.assertFalse(os.path.exists(self.output))

        graph = {"name": "store"}
        self.dump(graph)
        self.compress([self.json, self.input])

        self.assertTrue(os.path.exists(self.output))

    def test_field_lz(self) -> None:
        """
        Tests a simple FieldLZ graph
        """

        self.assertFalse(os.path.exists(self.output))

        graph = {"name": "interpret_as_le32", "successors": [{"name": "field_lz"}]}
        self.compress([json.dumps(graph), self.input])

        self.assertTrue(os.path.exists(self.output))

    def test_compression_level(self) -> None:
        """
        Tests setting global parameters
        """
        self.assertFalse(os.path.exists(self.output))

        graph = {
            "name": "interpret_as_le32",
            "successors": [{"name": "field_lz"}],
            "global_params": {"2": 1},
        }
        self.compress([json.dumps(graph), self.input])

        self.assertTrue(os.path.exists(self.output))

    def test_tokenize_with_params(self) -> None:
        """
        Tests a more complex tokenize graph that requires setting transform parameters.
        """
        self.assertFalse(os.path.exists(self.output))

        graph = {
            "name": "interpret_as_le32",
            "successors": [
                {
                    "name": "tokenize",
                    "int_params": {
                        # Parameter 0 is the stream type, 4 means numeric
                        "0": 4,
                        # Parameter 1 is whether or not to sort
                        "1": 1,
                    },
                    "successors": [
                        {
                            "name": "delta_int",
                            "successors": [
                                {
                                    "name": "field_lz",
                                }
                            ],
                        },
                        {"name": "field_lz"},
                    ],
                }
            ],
        }
        self.compress([json.dumps(graph), self.input])

        self.assertTrue(os.path.exists(self.output))
