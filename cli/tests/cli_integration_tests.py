# Copyright (c) Meta Platforms, Inc. and affiliates.

import sys
import unittest

import command_utils

from abstract_compression_test import (
    _BenchmarkBaseTest,
    _CompressDecompressBaseTest,
    _CsvBaseTest,
    _TrainBaseTest,
    _TrainInlineBaseTest,
)


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


class MLSelectorTest(_TrainBaseTest):
    """
    Test case for ml selector and compression using the trainer.
    """

    @property
    def input_dir_name(self) -> str:
        """
        Return the directory name for input sample files.

        This property determines where sample files are located:
        cli/tests/sample_files/ml_selector/

        Note: sample files are generated using the following command from
        tutorial in examples/ml_selector and taking the first file:

        ```
        buck2 run @//mode/opt examples/ml_selector:generate_data -- /tmp/ml_test_samples
        ```

        Returns:
            "ml_selector" as the input directory name
        """
        return "ml_selector"

    @property
    def compressor_profile_name(self) -> str:
        """
        Return the profile name to use for compression/training.

        Returns:
            "numeric-ml-selector-64" as the profile name
        """
        return "numeric-ml-selector-64"

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
