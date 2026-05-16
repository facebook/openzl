# Copyright (c) Meta Platforms, Inc. and affiliates.

"""
Comprehensive training tests for the CLI.

These tests exercise various ACE training configurations (different trainers,
chunking, inline training, ML selectors, etc.). They are NOT part of `make test`
because they are slow. Run them explicitly via `make test-train`.
"""

import os
import shutil
import struct
import sys
import tempfile
import unittest

import command_utils
from abstract_compression_test import (
    _CsvBaseTest,
    _MLBaseTest,
    _TrainBaseTest,
    _TrainInlineBaseTest,
    HAS_OPENZL_EXT,
)
from command_utils import (
    CompressorInfo,
    CompressorType,
    execute_compress,
    execute_decompress,
    execute_train,
)
from file_utils import file_contents_match, input_dir_path, profile_dir_path


class CsvGreedyTest(_CsvBaseTest):
    """
    Test case for CSV training and compression using the greedy trainer.

    This test demonstrates the train-compress-decompress workflow for CSV files
    using the greedy training algorithm. The greedy trainer optimizes compression
    by making locally optimal choices at each step.

    Sample files are located in cli/tests/sample_files/csv/
    Output files are stored in {output_dir_path}
    """

    @property
    def trainer_name(self) -> str | None:
        return "greedy"

    def test_train_compress_decompress(self):
        """
        Test the train, compress, and decompress workflow for CSV files using the greedy trainer.

        This test:
        1. Trains a compressor on the CSV files in cli/tests/sample_files/csv/ using the greedy trainer
        2. Saves the trained compressor to {output_dir_path}/trained_compressor.zlc
        3. Uses the trained compressor to compress and decompress the CSV files
        4. Verifies that the decompressed files match the originals
        """
        self.train_compress_decompress()


class CsvSaveAceStateTest(_CsvBaseTest):
    """
    Test case for CSV training with --save-ace-state flag.

    Verifies that training with --save-ace-state produces a compressor
    that correctly compresses and decompresses files.
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

    This test demonstrates the train-compress-decompress workflow for CSV files
    using the full-split training algorithm. The full-split trainer optimizes compression
    by analyzing the entire dataset before making decisions.

    Sample files are located in cli/tests/sample_files/csv/
    Output files are stored in {output_dir_path}/csv_full_split/
    """

    @property
    def trainer_name(self) -> str | None:
        return "full-split"

    def test_train_compress_decompress(self):
        """
        Test the train, compress, and decompress workflow for CSV files using the full-split trainer.

        This test:
        1. Trains a compressor on the CSV files in cli/tests/sample_files/csv/ using the full-split trainer
        2. Saves the trained compressor to {output_dir_path}/trained_compressor.zlc
        3. Uses the trained compressor to compress and decompress the CSV files
        4. Verifies that the decompressed files match the originals
        """
        self.train_compress_decompress()


class CsvBottomUpTest(_CsvBaseTest):
    """
    Test case for CSV training and compression using the bottom-up trainer.

    Sample files are located in cli/tests/sample_files/csv/
    Output files are stored in {output_dir_path}
    """

    @property
    def trainer_name(self) -> str | None:
        return "bottom-up"

    def test_train_compress_decompress(self):
        """
        Test the train, compress, and decompress workflow for CSV files using the bottom-up trainer.
        """
        self.train_compress_decompress()


class CsvChunkedTest(_CsvBaseTest):
    """
    Test case for CSV training and compression using the trainer with chunking.

    Sample files are located in cli/tests/sample_files/csv/
    Output files are stored in {output_dir_path}
    """

    @property
    def extra_args(self) -> str | None:
        return "--chunk-size 1M"

    def test_train_compress_decompress(self):
        """
        Test the train, compress, and decompress workflow for CSV files with chunking.
        """
        self.train_compress_decompress()


class Sddl2ChunkedTrainTest(unittest.TestCase):
    k_blocks = 12
    k_entries_per_block = 2048

    def setUp(self) -> None:
        self.output_dir_path = tempfile.mkdtemp()
        self.addCleanup(lambda: shutil.rmtree(self.output_dir_path, True))

        self.input_dir_path = os.path.join(self.output_dir_path, "input")
        os.makedirs(self.input_dir_path)

        self.description_path = os.path.join(
            self.output_dir_path, "repeated_blocks.sddl"
        )
        self.sample_path = os.path.join(self.input_dir_path, "sample.bin")
        self.trained_compressor_path = os.path.join(
            self.output_dir_path, "trained_compressor.zlc"
        )
        self.compressed_path = os.path.join(self.output_dir_path, "sample.zl")
        self.decompressed_path = os.path.join(self.output_dir_path, "sample.out")

        with open(self.description_path, "w", encoding="utf-8") as handle:
            handle.write(self._build_description())

        with open(self.sample_path, "wb") as handle:
            handle.write(self._build_sample())

    def _build_description(self) -> str:
        lines = [
            "record Header() {",
            "    magic: UInt32LE,",
            "    flag: Byte,",
            "}",
            "",
            "record Entry(flag) {",
            "    id: UInt32LE,",
            "    when flag {",
            "        optional: UInt16LE,",
            "    },",
            "    required: UInt64LE,",
            "}",
            "",
            "header: Header",
            "expect header.magic == 0xdeadbeef",
        ]

        for block in range(self.k_blocks):
            lines.append(
                f"block{block}: Entry(header.flag)[{self.k_entries_per_block}]"
            )

        return "\n".join(lines) + "\n"

    def _build_sample(self) -> bytes:
        payload = bytearray()
        payload += struct.pack("<I", 0xDEADBEEF)
        payload.append(1)

        for block in range(self.k_blocks):
            for entry in range(self.k_entries_per_block):
                value = block * self.k_entries_per_block + entry
                payload += struct.pack("<I", value)
                payload += struct.pack("<H", (value ^ 0x55AA) & 0xFFFF)
                payload += struct.pack("<Q", value * 17 + 3)

        return bytes(payload)

    def test_train_compress_decompress(self):
        training_compressor = CompressorInfo(
            compressor_str="sddl2",
            compressor_type=CompressorType.PROFILE,
        )
        trained_compressor = CompressorInfo(
            compressor_str=self.trained_compressor_path,
            compressor_type=CompressorType.FILE,
        )
        extra_args = f"--profile-arg {self.description_path} --chunk-size 256K"

        execute_train(
            compressor_info=training_compressor,
            uncompressed_dir=self.input_dir_path,
            trained_compressor_path=self.trained_compressor_path,
            extra_args=extra_args,
        )
        execute_compress(
            file_to_compress_path=self.sample_path,
            compressor_info=trained_compressor,
            compressed_file_path=self.compressed_path,
            extra_args=None,
        )
        execute_decompress(
            compressed_file_path=self.compressed_path,
            decompressed_file_path=self.decompressed_path,
        )

        self.assertTrue(file_contents_match(self.sample_path, self.decompressed_path))


@unittest.skipUnless(HAS_OPENZL_EXT, "requires openzl.ext module")
class MLDynamicSuccessorTest(_MLBaseTest):
    """
    Test case for ml selector with dynamic successors created from folder of serialized compressors.
    Sample files are located in cli/tests/sample_files/ml_selector/
    Serialized compressors are created dynamically
    """

    @property
    def extra_args(self) -> str | None:
        """Pass the serialized compressor folder via --profile-arg."""
        return f"--profile-arg {self.serialized_compressors_folder}"

    def test_dynamic_ml_successors(self):
        """
        Test train_compress_decompress works when there is ml selector successor in compressor.
        """
        default_compressor_info = CompressorInfo(
            compressor_str=self.compressor_profile_name,
            compressor_type=CompressorType.PROFILE,
        )

        # Train ml selector and save trained compressor
        execute_train(
            compressor_info=default_compressor_info,
            uncompressed_dir=input_dir_path(self.input_dir_name),
            trained_compressor_path=os.path.join(
                self.serialized_compressors_folder, "trained.cbor"
            ),
        )

        # Train ml selector that has a ml selector as a successor
        execute_train(
            compressor_info=default_compressor_info,
            uncompressed_dir=input_dir_path(self.input_dir_name),
            trained_compressor_path=self.compressor_info.compressor_str,
            trainer_name=self.trainer_name,
            extra_args=self.extra_args,
        )

        self.compress_and_decompress_samples()

    def test_compression_ratios(self):
        """
        Create trained compressor using dynamic successors that match static successors in numeric-ml-selector-64 profile.
        Resulting compressed files should have the same compression ratio as directly using the numeric-ml-selector-64 profile.
        """
        from file_utils import file_contents_match

        default_compressor_info = CompressorInfo(
            compressor_str=self.compressor_profile_name,
            compressor_type=CompressorType.PROFILE,
        )

        # Train with static successors (no extra_args)
        static_trained_path = os.path.join(self.output_dir_path, "static_trained.zlc")
        execute_train(
            compressor_info=default_compressor_info,
            uncompressed_dir=input_dir_path(self.input_dir_name),
            trained_compressor_path=static_trained_path,
            trainer_name=self.trainer_name,
            extra_args=None,
        )

        # Train with dynamic successors (with extra_args)
        dynamic_trained_path = os.path.join(self.output_dir_path, "dynamic_trained.zlc")
        execute_train(
            compressor_info=default_compressor_info,
            uncompressed_dir=input_dir_path(self.input_dir_name),
            trained_compressor_path=dynamic_trained_path,
            trainer_name=self.trainer_name,
            extra_args=self.extra_args,
        )

        # Compress samples with both trained compressors and compare outputs
        static_compressor_info = CompressorInfo(
            compressor_str=static_trained_path,
            compressor_type=CompressorType.FILE,
        )
        dynamic_compressor_info = CompressorInfo(
            compressor_str=dynamic_trained_path,
            compressor_type=CompressorType.FILE,
        )

        for sample in self.input_samples:
            static_compressed_path = sample.compressed_file_path + "_static"
            execute_compress(
                file_to_compress_path=sample.orig_file_path,
                compressor_info=static_compressor_info,
                compressed_file_path=static_compressed_path,
                extra_args=None,
            )

            dynamic_compressed_path = sample.compressed_file_path + "_dynamic"
            execute_compress(
                file_to_compress_path=sample.orig_file_path,
                compressor_info=dynamic_compressor_info,
                compressed_file_path=dynamic_compressed_path,
                extra_args=None,
            )

            self.assertTrue(
                file_contents_match(static_compressed_path, dynamic_compressed_path),
                f"Compressed files differ for {sample.orig_file_path}: "
                f"static={os.path.getsize(static_compressed_path)} bytes, "
                f"dynamic={os.path.getsize(dynamic_compressed_path)} bytes",
            )

        print("Compressed files are identical between static and dynamic successors.")


@unittest.skipUnless(HAS_OPENZL_EXT, "requires openzl.ext module")
class MLSelectorTest(_MLBaseTest):
    """
    Test case for ml selector and compression using the trainer.
    Sample files are located in cli/tests/sample_files/ml_selector/
    """

    def test_train_compress_decompress(self):
        """
        Test the train, compress, and decompress workflow for numeric 64 bit files using the ml selector trainer.

        This test:
        1. Trains a compressor on files in cli/tests/sample_files/ml_selector/
        2. Saves the trained compressor to {output_dir_path}/trained_compressor.zlc
        3. Uses the trained compressor to compress and decompress the files
        4. Verifies that the decompressed files match the originals
        """
        self.train_compress_decompress()


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
        """
        Test the train, compress, and decompress workflow for Parquet files using the clustering trainer.
        """
        self.train_compress_decompress()


class ParquetChunkedTest(ParquetTest):
    """
    Verify that --chunk-size is wired through to the parquet profile.

    Trains twice — once with the default chunk size and once with an explicit
    --chunk-size 2M — then checks that the serialized compressors differ
    (the chunk size is baked into the profile) and that both round-trip
    correctly.
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


class U16TrainInlineTest(_TrainInlineBaseTest):
    @property
    def input_file_name(self) -> str:
        return "u16/zigzag_1000.bin"

    @property
    def compressor_profile_name(self) -> str:
        return "le-u16"

    def test_train_inline(self) -> None:
        self.train_inline()


class AceTrainInlineTest(_TrainInlineBaseTest):
    @property
    def input_file_name(self) -> str:
        return "ace/newlines.txt"

    @property
    def compressor_profile_name(self) -> str:
        return "serial"

    def test_train_inline(self) -> None:
        self.train_inline()


class U8TrainTest(_TrainBaseTest):
    """
    Test case for u8 profile with ACE training.

    This test verifies that the u8 profile can be trained using ACE
    (Automated Compressor Explorer) and that the trained compressor works correctly.
    Sample files are located in cli/tests/sample_files/u8/
    """

    @property
    def input_dir_name(self) -> str:
        return "u8"

    @property
    def compressor_profile_name(self) -> str:
        return "u8"

    def test_train_compress_decompress(self):
        """
        Test the train, compress, and decompress workflow for u8 profile.
        """
        self.train_compress_decompress()


class SDDL2TrainTest(_TrainBaseTest):
    """
    Test case for SDDL2 compression, decompression, and training using the clustering trainer.

    This test verifies that a simple format described by SDDL2 can be trained,
    compressed and decompressed correctly. The format includes a header
    with an array of entries with a mix of fixed and optional fields.
    """

    @property
    def input_dir_name(self) -> str:
        return "sddl2"

    @property
    def compressor_profile_name(self) -> str:
        return "sddl2"

    @property
    def extra_args(self) -> str | None:
        return (
            "--profile-arg "
            + profile_dir_path(self.input_dir_name)
            + "/simple_description.sddl"
        )

    def test_train_compress_decompress(self):
        """
        Test the train, compress, and decompress workflow for files described by SDDL2.
        """
        self.train_compress_decompress()


def main():
    """
    Run the training test suite.

    Usage: python3 cli_train_tests.py <path-to-zli-binary>
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
