# Copyright (c) Meta Platforms, Inc. and affiliates.

import os
import sys
import unittest

import command_utils
from abstract_compression_test import (
    _BenchmarkBaseTest,
    _CompressDecompressBaseTest,
    _CsvBaseTest,
    _MLBaseTest,
    _TrainBaseTest,
    _TrainInlineBaseTest,
)
from command_utils import (
    CompressorInfo,
    CompressorType,
    execute_compress,
    execute_decompress,
    execute_train,
)
from file_utils import input_dir_path, profile_dir_path


class SerialTest(_CompressDecompressBaseTest):
    """
    Test case for ZSTD compression and decompression functionality via ZStrong CLI.

    This test uses the serial profile for compression and decompression.
    Sample files are located in cli/tests/sample_files/serial/
    """

    @property
    def input_dir_name(self) -> str:
        """
        Return the directory name for input sample files.

        This property determines where sample files are located:
        cli/tests/sample_files/{input_dir_name}/
        """
        return "serial"

    @property
    def compressor_profile_name(self) -> str:
        """
        Return the profile name to use for compression.

        Returns:
            "serial" as the profile name
        """
        return "serial"

    def test_compress_decompress(self):
        """
        Test that files can be compressed and then decompressed correctly.

        This test:
        1. Compresses all files in cli/tests/sample_files/serial/
        2. Decompresses the compressed files
        3. Verifies that the decompressed files match the originals
        """
        self.compress_and_decompress_samples()


class CsvTest(_CsvBaseTest):
    """
    Test case for CSV training and compression using the default trainer.

    This test demonstrates the train-compress-decompress workflow for CSV files
    using the default training algorithm.
    Sample files are located in cli/tests/sample_files/csv/
    Output files are stored in a temporary directory
    """

    def test_train_compress_decompress(self):
        """
        Test the train, compress, and decompress workflow for CSV files using the default trainer.

        This test:
        1. Trains a compressor on the CSV files in cli/tests/sample_files/csv/ using the default trainer
        2. Saves the trained compressor to {output_dir_path}/trained_compressor.zlc
        3. Uses the trained compressor to compress and decompress the CSV files
        4. Verifies that the decompressed files match the originals
        """
        self.train_compress_decompress()


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
    Test case for CSV training and compression using the bottom-up eedy trainer.

    This test demonstrates the train-compress-decompress workflow for CSV files
    using the greedy training algorithm. The greedy trainer optimizes compression
    by making locally optimal choices at each step.

    Sample files are located in cli/tests/sample_files/csv/
    Output files are stored in {output_dir_path}
    """

    @property
    def trainer_name(self) -> str | None:
        return "bottom-up"

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


class CsvChunkedTest(_CsvBaseTest):
    """
    Test case for CSV training and compression using the trainer with chunking.

    This test demonstrates the train-compress-decompress workflow for CSV files
    using including chunking the input data.

    Sample files are located in cli/tests/sample_files/csv/
    Output files are stored in {output_dir_path}
    """

    @property
    def extra_args(self) -> str | None:
        return "--chunk-size-mb 1"

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
        """
        Return the directory name for input sample files.

        This property determines where sample files are located:
        cli/tests/sample_files/parquet/

        Returns:
            "parquet" as the input directory name
        """
        return "parquet"

    @property
    def compressor_profile_name(self) -> str:
        """
        Return the profile name to use for compression/training.

        Returns:
            "parquet" as the profile name
        """
        return "parquet"

    def test_train_compress_decompress(self):
        """
        Test the train, compress, and decompress workflow for Parquet files using the clustering trainer.

        This test:
        1. Trains a compressor on the Parquet files in cli/tests/sample_files/parquet/
        2. Saves the trained compressor to {output_dir_path}/trained_compressor.zlc
        3. Uses the trained compressor to compress and decompress the Parquet files
        4. Verifies that the decompressed files match the originals
        """
        self.train_compress_decompress()


class CsvTrainInlineTest(_TrainInlineBaseTest):
    @property
    def input_file_name(self) -> str:
        return "csv/input_experiments.csv"

    @property
    def compressor_profile_name(self) -> str:
        return "csv"

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


class U8Test(_CompressDecompressBaseTest):
    """
    Test case for u8 profile compression and decompression.

    This test verifies that the u8 (unsigned 8-bit) profile
    can compress and decompress 8-bit data correctly.
    Sample files are located in cli/tests/sample_files/u8/
    """

    @property
    def input_dir_name(self) -> str:
        return "u8"

    @property
    def compressor_profile_name(self) -> str:
        return "u8"

    def test_compress_decompress(self):
        """
        Test that u8 profile can compress and decompress 8-bit data.

        This test:
        1. Compresses all files in cli/tests/sample_files/u8/
        2. Decompresses the compressed files
        3. Verifies that the decompressed files match the originals
        """
        self.compress_and_decompress_samples()


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

        This test:
        1. Trains a compressor on the u8 files in cli/tests/sample_files/u8/ using ACE
        2. Saves the trained compressor to {output_dir_path}/trained_compressor.zlc
        3. Uses the trained compressor to compress and decompress the u8 files
        4. Verifies that the decompressed files match the originals
        """
        self.train_compress_decompress()


class BenchmarkCsvCompressionTest(_BenchmarkBaseTest):
    """
    Test case for benchmarking compression.
    """

    @property
    def input_dir_name(self) -> str:
        return "csv"

    @property
    def compressor_profile_name(self) -> str:
        return "csv"

    def test_benchmark(self):
        self.benchmark()


class TraceTest(_CompressDecompressBaseTest):
    """
    Test case for compression and decompression with tracing enabled.

    This test verifies that the --trace and --trace-streams-dir flags work
    correctly during compress and decompress without crashing, and that
    trace output files are actually created.
    """

    @property
    def input_dir_name(self) -> str:
        return "trace"

    @property
    def compressor_profile_name(self) -> str:
        return "csv"

    def test_compress_decompress_with_trace(self):
        """
        Test that compress and decompress with --trace flags produce trace files
        and roundtrip correctly.

        This test:
        1. Compresses a CSV sample with --trace and --trace-streams-dir
        2. Asserts the compress trace CBOR file exists and is non-empty
        3. Decompresses with --trace
        4. Asserts the decompress trace CBOR file exists and is non-empty
        5. Verifies the decompressed file matches the original (roundtrip check)
        """
        sample = self.input_samples[0]

        compress_trace_path = os.path.join(self.output_dir_path, "compress_trace.cbor")
        decompress_trace_path = os.path.join(
            self.output_dir_path, "decompress_trace.cbor"
        )
        streams_dir = os.path.join(self.output_dir_path, "streams")
        os.makedirs(streams_dir, exist_ok=True)

        execute_compress(
            file_to_compress_path=sample.orig_file_path,
            compressor_info=self.compressor_info,
            compressed_file_path=sample.compressed_file_path,
            extra_args=f"--trace {compress_trace_path} --trace-streams-dir {streams_dir}",
        )

        self.assertTrue(
            os.path.exists(sample.compressed_file_path),
            "Compressed file was not created",
        )
        self.assertTrue(
            os.path.exists(compress_trace_path),
            "Compress trace file was not created",
        )
        self.assertGreater(
            os.path.getsize(compress_trace_path),
            0,
            "Compress trace file is empty",
        )
        self.assertGreater(
            len(os.listdir(streams_dir)),
            0,
            "Compress streams dir is empty",
        )

        decompress_streams_dir = os.path.join(
            self.output_dir_path, "decompress_streams"
        )
        os.makedirs(decompress_streams_dir, exist_ok=True)

        execute_decompress(
            compressed_file_path=sample.compressed_file_path,
            decompressed_file_path=sample.decompressed_file_path,
            extra_args=f"--trace {decompress_trace_path} --trace-streams-dir {decompress_streams_dir}",
        )

        self.assertTrue(
            os.path.exists(decompress_trace_path),
            "Decompress trace file was not created",
        )
        self.assertGreater(
            os.path.getsize(decompress_trace_path),
            0,
            "Decompress trace file is empty",
        )
        self.assertGreater(
            len(os.listdir(decompress_streams_dir)),
            0,
            "Decompress streams dir is empty",
        )

        self.assertTrue(
            sample.original_matches_decompressed,
            f"Decompressed file does not match original: {sample.orig_file_path}",
        )


class StrictModeTest(_CompressDecompressBaseTest):
    """
    Test case for strict mode behavior.

    This test verifies that:
    1. By default (permissive mode), compression succeeds even when using a
       mismatched profile (e.g., le-u64 profile on data not divisible by 8)
    2. With --strict flag, compression fails on mismatched data

    The test uses the le-u64 profile (64-bit little-endian unsigned integers)
    to compress data whose size is NOT a multiple of 8 bytes. This should:
    - Succeed in permissive mode (default) by falling back to generic compression
    - Fail in strict mode because the input size doesn't match 64-bit alignment
    """

    @property
    def input_dir_name(self) -> str:
        return "serial"

    @property
    def compressor_profile_name(self) -> str:
        # Use le-u64 profile which expects input size to be multiple of 8 bytes
        return "le-u64"

    def test_permissive_mode_succeeds(self):
        """
        Test that compression succeeds in permissive mode (default).

        This verifies that when using a mismatched profile (le-u64 on non-aligned data),
        the compression falls back to generic compression and succeeds.
        """
        self.compress_and_decompress_samples()

    def test_strict_mode_fails(self):
        """
        Test that compression fails in strict mode with mismatched data.

        This verifies that when using --strict flag with a mismatched profile,
        the compression fails instead of falling back to generic compression.

        Note: Only files whose size is NOT a multiple of 8 AND larger than
        a minimum threshold will trigger the failure. Very small files may
        be handled differently by the compression pipeline.
        """
        from command_utils import execute_command

        failed_count = 0
        for sample in self.input_samples:
            # Attempt compression with --strict flag
            cflag = self.compressor_info.compressor_type.value
            cstr = self.compressor_info.compressor_str

            compress_args = f"compress {sample.orig_file_path} --{cflag} {cstr} -o {sample.compressed_file_path} --strict"

            result = execute_command(compress_args)

            if result != 0:
                failed_count += 1
                print(f"Strict mode correctly failed for {sample.orig_file_path}")

        # At least one file should fail in strict mode
        self.assertGreater(
            failed_count,
            0,
            "Expected at least one compression to fail in strict mode",
        )

        print(
            f"Verified that strict mode fails: {failed_count} file(s) failed as expected"
        )


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
        Test the train, compress, and decompress workflow for files decribed by SDDL2.
        """
        self.train_compress_decompress()


def main():
    """
    Run the test suite with proper command line arguments.

    This script expects the CLI binary path as the first command line argument.
    The CLI binary path can be provided either when built with buck or make:
    - Buck: $(location cli:zli)
    - Make: "${PROJECT_BINARY_DIR}/cli/zli"

    The CLI binary path is used to execute commands in command_utils.py.

    Directory Structure:
    - Test classes define a compressor_profile_name property (e.g., "csv", "serial")
    - Sample files are located in cli/tests/sample_files/{input_dir_name}/
    - Test outputs are stored in a temp directory.

    To add a new test, see the detailed instructions in README.md.
    """
    # Check if CLI path is provided
    if len(sys.argv) < 2:
        raise ValueError(
            "CLI binary path must be provided as the first command line argument"
        )

    # Set the CLI path in command_utils.py
    command_utils.CLI_CPP = sys.argv[1]

    # Check if a specific test is specified
    if len(sys.argv) > 2:
        test_arg = sys.argv[2]
        sys.argv = [sys.argv[0], test_arg]
    else:
        sys.argv = [sys.argv[0]]

    unittest.main()


if __name__ == "__main__":
    main()
