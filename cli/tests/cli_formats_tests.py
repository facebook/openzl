# Copyright (c) Meta Platforms, Inc. and affiliates.

"""
Format-specific tests for the CLI.

These tests exercise compression profiles that have dedicated parsers (CSV,
Parquet, etc.). They verify that format-aware compression works end-to-end
including training with various trainers, chunking, and profile arguments.

Run via `make test-formats`.
"""

import os
import sys
import unittest

import command_utils
from abstract_compression_test import (
    _CompressDecompressBaseTest,
    _CsvBaseTest,
    _TrainBaseTest,
)
from command_utils import (
    CompressorInfo,
    CompressorType,
    execute_compress,
    execute_decompress,
    execute_train,
)
from file_utils import file_contents_match, input_dir_path


class CsvTest(_CsvBaseTest):
    """
    Test case for CSV training and compression using the default trainer.
    """

    def test_train_compress_decompress(self):
        self.train_compress_decompress()


class CsvGreedyTest(_CsvBaseTest):
    """
    Test case for CSV training and compression using the greedy trainer.
    """

    @property
    def trainer_name(self) -> str | None:
        return "greedy"

    def test_train_compress_decompress(self):
        self.train_compress_decompress()


class CsvSaveAceStateTest(_CsvBaseTest):
    """
    Test case for CSV training with --save-ace-state flag.
    """

    def test_train_compress_decompress(self):
        execute_train(
            compressor_info=self.training_compressor_info,
            uncompressed_dir=input_dir_path(self.input_dir_name),
            trained_compressor_path=self.compressor_info.compressor_str,
            extra_args="--save-ace-state",
        )
        self.compress_and_decompress_samples()


class CsvFullSplitTest(_CsvBaseTest):
    """
    Test case for CSV training and compression using the full-split trainer.
    """

    @property
    def trainer_name(self) -> str | None:
        return "full-split"

    def test_train_compress_decompress(self):
        self.train_compress_decompress()


class CsvBottomUpTest(_CsvBaseTest):
    """
    Test case for CSV training and compression using the bottom-up trainer.
    """

    @property
    def trainer_name(self) -> str | None:
        return "bottom-up"

    def test_train_compress_decompress(self):
        self.train_compress_decompress()


class CsvChunkedTest(_CsvBaseTest):
    """
    Test case for CSV training and compression with chunking.
    """

    @property
    def extra_args(self) -> str | None:
        return "--chunk-size 1M"

    def test_train_compress_decompress(self):
        self.train_compress_decompress()


class CsvAlternativeSeparatorTest(_CompressDecompressBaseTest):
    """
    Test case for CSV compression and decompression with an alternate separator.
    """

    @property
    def input_dir_name(self) -> str:
        return "tbl"

    @property
    def compressor_profile_name(self) -> str:
        return "csv"

    @property
    def extra_args(self) -> str | None:
        return "--profile-arg '|'"

    def test_compress_decompress(self):
        self.compress_and_decompress_samples()


class ParquetTest(_TrainBaseTest):
    """
    Parquet compression tests with training.
    """

    @property
    def input_dir_name(self) -> str:
        return "parquet"

    @property
    def compressor_profile_name(self) -> str:
        return "parquet"

    def test_train_compress_decompress(self):
        self.train_compress_decompress()


class ParquetChunkedTest(ParquetTest):
    """
    Verify that --chunk-size is wired through to the parquet profile.
    """

    def test_chunk_size_affects_output(self):
        profile = CompressorInfo(
            compressor_str=self.compressor_profile_name,
            compressor_type=CompressorType.PROFILE,
        )

        default_trained = os.path.join(self.output_dir_path, "default.zlc")
        execute_train(
            compressor_info=profile,
            uncompressed_dir=input_dir_path(self.input_dir_name),
            trained_compressor_path=default_trained,
        )

        chunked_trained = os.path.join(self.output_dir_path, "chunked.zlc")
        execute_train(
            compressor_info=profile,
            uncompressed_dir=input_dir_path(self.input_dir_name),
            trained_compressor_path=chunked_trained,
            extra_args="--chunk-size 2M",
        )

        with open(default_trained, "rb") as f:
            default_bytes = f.read()
        with open(chunked_trained, "rb") as f:
            chunked_bytes = f.read()
        self.assertNotEqual(
            default_bytes,
            chunked_bytes,
            "Trained compressors should differ when --chunk-size is changed",
        )

        sample = self.input_samples[0]
        for trained_path, label in [
            (default_trained, "default"),
            (chunked_trained, "chunked"),
        ]:
            trained = CompressorInfo(
                compressor_str=trained_path,
                compressor_type=CompressorType.FILE,
            )
            compressed = sample.compressed_file_path + f".{label}"
            execute_compress(
                file_to_compress_path=sample.orig_file_path,
                compressor_info=trained,
                compressed_file_path=compressed,
                extra_args=None,
            )
            decompressed = compressed + ".out"
            execute_decompress(
                compressed_file_path=compressed,
                decompressed_file_path=decompressed,
            )
            self.assertTrue(
                file_contents_match(sample.orig_file_path, decompressed),
                f"Round-trip failed for {label} path",
            )


def main():
    """
    Run the format-specific test suite.

    Usage: python3 cli_formats_tests.py <path-to-zli-binary>
    """
    if len(sys.argv) < 2:
        raise ValueError(
            "CLI binary path must be provided as the first command line argument"
        )

    command_utils.CLI_CPP = sys.argv[1]

    if len(sys.argv) > 2:
        test_arg = sys.argv[2]
        sys.argv = [sys.argv[0], test_arg]
    else:
        sys.argv = [sys.argv[0]]

    unittest.main()


if __name__ == "__main__":
    main()
