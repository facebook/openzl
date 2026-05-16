# Copyright (c) Meta Platforms, Inc. and affiliates.

import os
import shutil
import struct
import sys
import tempfile
import unittest

import command_utils
from abstract_compression_test import (
    _BenchmarkBaseTest,
    _CompressDecompressBaseTest,
    _CsvBaseTest,
)
from command_utils import (
    CompressorInfo,
    CompressorType,
    execute_compress,
    execute_decompress,
)
from file_utils import file_contents_match, input_dir_path, profile_dir_path


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
    trace output files are actually created. Tests that need full stream
    traces must opt out of StoreOnExpansion explicitly.
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
        1. Compresses a CSV sample with --trace, --trace-streams-dir, and
           --no-store-on-expansion
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
            extra_args=(
                f"--trace {compress_trace_path} "
                f"--trace-streams-dir {streams_dir} "
                "--no-store-on-expansion"
            ),
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

    def test_trace_does_not_change_compressed_output(self):
        """
        Test that compression tracing does not change the compressed bytes.

        This uses a checked-in random sample that exercises StoreOnExpansion
        for the serial profile. The traced output should match the default
        output, not the --no-store-on-expansion output.
        """
        random_input_path = os.path.join(input_dir_path("u8"), "random_u8.bin")

        compressor_info = CompressorInfo(
            compressor_str="serial",
            compressor_type=CompressorType.PROFILE,
        )
        plain_compressed_path = os.path.join(
            self.output_dir_path, "plain_random_input.zl"
        )
        traced_compressed_path = os.path.join(
            self.output_dir_path, "traced_random_input.zl"
        )
        no_store_compressed_path = os.path.join(
            self.output_dir_path, "no_store_random_input.zl"
        )
        trace_path = os.path.join(self.output_dir_path, "random_trace.cbor")
        streams_dir = os.path.join(self.output_dir_path, "random_trace_streams")
        os.makedirs(streams_dir, exist_ok=True)

        execute_compress(
            file_to_compress_path=random_input_path,
            compressor_info=compressor_info,
            compressed_file_path=plain_compressed_path,
            extra_args=None,
        )
        execute_compress(
            file_to_compress_path=random_input_path,
            compressor_info=compressor_info,
            compressed_file_path=traced_compressed_path,
            extra_args=f"--trace {trace_path} --trace-streams-dir {streams_dir}",
        )
        execute_compress(
            file_to_compress_path=random_input_path,
            compressor_info=compressor_info,
            compressed_file_path=no_store_compressed_path,
            extra_args="--no-store-on-expansion",
        )

        self.assertFalse(
            file_contents_match(plain_compressed_path, no_store_compressed_path),
            "Test input did not exercise StoreOnExpansion",
        )

        self.assertTrue(
            file_contents_match(plain_compressed_path, traced_compressed_path),
            "Compression tracing changed the compressed output",
        )
        self.assertTrue(
            os.path.exists(trace_path),
            "Compress trace file was not created",
        )
        self.assertGreater(
            os.path.getsize(trace_path),
            0,
            "Compress trace file is empty",
        )


class NumericSegmentationTest(unittest.TestCase):
    """
    Test case for numeric profile auto-segmentation via the CLI.

    Generates binary numeric data, compresses with numeric profiles,
    decompresses, and verifies round-trip correctness. Tests multiple
    element widths and chunk sizes to exercise the segmenter.
    """

    def setUp(self) -> None:
        import shutil
        import struct
        import tempfile

        self.tmpdir = tempfile.mkdtemp()
        self.addCleanup(lambda: shutil.rmtree(self.tmpdir, True))

        # Generate ~2MB of data per profile so --chunk-size 1M triggers
        # multi-chunk segmentation (2 chunks).
        self.test_files: dict[str, dict] = {}
        target_bytes = 2 * 1000 * 1000  # 2 MB
        configs = [
            ("u8", "B", 1, "u8"),
            ("le-u16", "H", 2, "le-u16"),
            ("le-i32", "i", 4, "le-i32"),
            ("le-u64", "Q", 8, "le-u64"),
        ]
        for profile, fmt, elt_size, name in configs:
            n = target_bytes // elt_size
            max_val = 2 ** (elt_size * 8)
            data = struct.pack(f"<{n}{fmt}", *[i % max_val for i in range(n)])
            path = os.path.join(self.tmpdir, f"{name}.bin")
            with open(path, "wb") as f:
                f.write(data)
            self.test_files[name] = {
                "path": path,
                "profile": profile,
                "size": len(data),
            }

    def _round_trip(
        self, profile: str, input_path: str, extra_args: str | None = None
    ) -> None:
        """Compress, decompress, and verify round-trip for a single file."""
        compressed_path = input_path + ".zl"
        decompressed_path = input_path + ".rt"

        compressor_info = CompressorInfo(
            compressor_str=profile,
            compressor_type=CompressorType.PROFILE,
        )
        execute_compress(
            file_to_compress_path=input_path,
            compressor_info=compressor_info,
            compressed_file_path=compressed_path,
            extra_args=extra_args,
        )
        execute_decompress(
            compressed_file_path=compressed_path,
            decompressed_file_path=decompressed_path,
        )
        from file_utils import file_contents_match

        self.assertTrue(
            file_contents_match(input_path, decompressed_path),
            f"Round-trip failed for profile {profile} on {input_path}",
        )

    def test_numeric_profiles_roundtrip(self) -> None:
        """Test that all numeric profiles compress and decompress correctly."""
        for name, info in self.test_files.items():
            with self.subTest(profile=name):
                self._round_trip(info["profile"], info["path"])

    def test_numeric_profiles_with_chunk_size(self) -> None:
        """Test numeric profiles with --chunk-size 1M on 2MB data (forces 2 chunks)."""
        for name, info in self.test_files.items():
            with self.subTest(profile=name):
                self._round_trip(
                    info["profile"],
                    info["path"],
                    extra_args="--chunk-size 1M",
                )


class SerialSegmentationTest(unittest.TestCase):
    """
    Test case for the serial profile's auto-segmentation via the CLI.

    Generates raw byte data, compresses with the `serial` profile, decompresses,
    and verifies round-trip correctness with and without --chunk-size.
    """

    def setUp(self) -> None:
        import shutil
        import tempfile

        self.tmpdir = tempfile.mkdtemp()
        self.addCleanup(lambda: shutil.rmtree(self.tmpdir, True))

        # Generate ~2MB of data so --chunk-size 1M triggers multi-chunk
        # segmentation (2 chunks).
        target_bytes = 2 * 1000 * 1000  # 2 MB
        data = bytes((i % 256) for i in range(target_bytes))
        self.input_path: str = os.path.join(self.tmpdir, "serial.bin")
        with open(self.input_path, "wb") as f:
            f.write(data)

    def _round_trip(self, extra_args: str | None = None) -> None:
        compressed_path = self.input_path + ".zl"
        decompressed_path = self.input_path + ".rt"

        compressor_info = CompressorInfo(
            compressor_str="serial",
            compressor_type=CompressorType.PROFILE,
        )
        execute_compress(
            file_to_compress_path=self.input_path,
            compressor_info=compressor_info,
            compressed_file_path=compressed_path,
            extra_args=extra_args,
        )
        execute_decompress(
            compressed_file_path=compressed_path,
            decompressed_file_path=decompressed_path,
        )
        from file_utils import file_contents_match

        self.assertTrue(
            file_contents_match(self.input_path, decompressed_path),
            f"Round-trip failed for serial profile on {self.input_path}",
        )

    def test_serial_default_chunk_size(self) -> None:
        """Default chunk size (16 MiB) on 2MB data → single-chunk segmentation."""
        self._round_trip()

    def test_serial_with_chunk_size(self) -> None:
        """--chunk-size 1M on 2MB data forces multi-chunk segmentation."""
        self._round_trip(extra_args="--chunk-size 1M")


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


class ChunkSizeBinarySuffixTest(_CompressDecompressBaseTest):
    """
    Test that --chunk-size accepts binary suffixes (KiB, MiB, etc.)
    through the checked integer parsing.
    """

    @property
    def input_dir_name(self) -> str:
        return "serial"

    @property
    def compressor_profile_name(self) -> str:
        return "serial"

    @property
    def extra_args(self) -> str | None:
        return "--chunk-size 512KiB"

    def test_compress_decompress(self):
        self.compress_and_decompress_samples()


class InvalidChunkSizeTest(unittest.TestCase):
    """
    Test that --chunk-size with an invalid suffix is rejected by the CLI.
    """

    def setUp(self) -> None:
        self.tmpdir = tempfile.mkdtemp()
        self.addCleanup(lambda: shutil.rmtree(self.tmpdir, True))

    def test_invalid_suffix_rejected(self):
        sample_dir = os.path.join(self.tmpdir, "input")
        os.makedirs(sample_dir)
        sample_path = os.path.join(sample_dir, "dummy.bin")
        with open(sample_path, "wb") as f:
            f.write(b"\x00" * 1024)

        compressed_path = os.path.join(self.tmpdir, "out.zl")
        result = command_utils.execute_command(
            f"compress {sample_path} --profile serial "
            f"-o {compressed_path} --chunk-size 1XYZ"
        )
        self.assertNotEqual(result, 0, "CLI should reject invalid suffix 'XYZ'")


class VersionTest(unittest.TestCase):
    """Test that --version and -V flags work correctly."""

    def test_version_exits_zero(self):
        result = command_utils.execute_command("--version")
        self.assertEqual(result, 0)

    def test_short_version_exits_zero(self):
        result = command_utils.execute_command("-V")
        self.assertEqual(result, 0)

    def test_version_and_short_version_are_identical(self):
        import subprocess

        version_out = subprocess.check_output(
            f"{command_utils.CLI_CPP} --version", shell=True
        ).decode()
        short_out = subprocess.check_output(
            f"{command_utils.CLI_CPP} -V", shell=True
        ).decode()
        self.assertEqual(version_out, short_out)


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
