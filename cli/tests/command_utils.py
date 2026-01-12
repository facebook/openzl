# Copyright (c) Meta Platforms, Inc. and affiliates.

import os
import subprocess
from dataclasses import dataclass
from enum import Enum

# CLI executable path will be set by cli_integration_tests.py
# This is the path to the ZStrong CLI binary (zli) that will be used for testing
CLI_CPP: str | None = None


class CompressorType(str, Enum):
    """
    Enum for compressor type. Value is the flag name to pass to the CLI.

    Values:
        FILE: Use a compressor from a file (--compressor flag)
        PROFILE: Use a built-in compression profile (--profile flag)
    """

    FILE = "compressor"  # --compressor
    PROFILE = "profile"  # --profile


@dataclass(frozen=True)
class CompressorInfo:
    """
    Data class for compressor information.

    This class encapsulates the information needed to specify a compressor
    to the ZStrong CLI, including whether it's a file-based compressor or
    a built-in profile.

    Attributes:
        compressor_str: String to pass to the CLI for the compressor
            - For FILE type: Path to the compressor file (e.g., "{output_dir_path}/trained_compressor.zlc")
            - For PROFILE type: Name of the built-in profile (e.g., "zstd", "csv")
        compressor_type: Type of compressor to use (FILE or PROFILE),
            determines which flag to use (--compressor or --profile)
    """

    compressor_str: str  # file path or profile name
    compressor_type: CompressorType


def execute_command(args: str) -> int:
    """
    Execute a CLI command with the given arguments.

    This function runs the ZStrong CLI (zli) with the specified arguments.
    The CLI path must be set before calling this function.

    Args:
        args: Command line arguments to pass to the CLI executable

    Returns:
        The return code from the command execution

    Raises:
        ValueError: If the CLI executable path is not set
    """
    print("Executing command: ", args)
    if not CLI_CPP:
        raise ValueError("CLI executable path not set")

    return subprocess.call(f"{CLI_CPP} {args}", shell=True)


def execute_benchmark(
    file_to_compress_path: str, compressor_info: CompressorInfo, extra_args: str | None
) -> None:
    """
    Execute the benchmark command to benchmark a file.

    This function runs the ZStrong CLI's benchmark command to benchmark the
    specified file using the provided compressor configuration.

    File paths in the test framework:
    - Input file: cli/tests/sample_files/{input_dir_name}/{filename}
    - Compressed file: {output_dir_path}/compressed/{filename}.zl

    Args:
        file_to_compress_path: Path to the file to compress
        compressor_info: Configuration for the compressor to use
        extra_args: Additional arguments to pass to the compress command (optional)

    Raises:
        ValueError: If the compression fails or the compressed file is not created
    """
    cflag = compressor_info.compressor_type.value
    cstr = compressor_info.compressor_str

    benchmark_args = (
        f"benchmark {str(file_to_compress_path)} --{cflag} {cstr} {extra_args or ''}"
    )

    if execute_command(benchmark_args) != 0:
        raise ValueError("Executing benchmark command failed")

    print(f"Used {cflag} {cstr} to benchmark compression on {file_to_compress_path}")


def execute_compress(
    file_to_compress_path: str,
    compressor_info: CompressorInfo,
    compressed_file_path: str,  # where to write the compressed file
    extra_args: str | None,
) -> None:
    """
    Execute the compress command to compress a file.

    This function runs the ZStrong CLI's compress command to compress the
    specified file using the provided compressor configuration.

    File paths in the test framework:
    - Input file: cli/tests/sample_files/{input_dir_name}/{filename}
    - Compressed file: {output_dir_path}/compressed/{filename}.zl

    Args:
        file_to_compress_path: Path to the file to compress
        compressor_info: Configuration for the compressor to use
        compressed_file_path: Path where the compressed file will be saved
        extra_args: Additional arguments to pass to the compress command (optional)

    Raises:
        ValueError: If the compression fails or the compressed file is not created
    """
    cflag = compressor_info.compressor_type.value
    cstr = compressor_info.compressor_str

    compress_args = f"compress {str(file_to_compress_path)} --{cflag} {cstr} -o {str(compressed_file_path)} {extra_args or ''}"

    if execute_command(compress_args) != 0:
        raise ValueError("Executing compress command failed")

    if not os.path.exists(compressed_file_path):
        raise ValueError("Compressed file was not created")

    print(
        f"Used {cflag} {cstr} to compress {file_to_compress_path} saved to {compressed_file_path}"
    )


def execute_decompress(
    compressed_file_path: str,
    decompressed_file_path: str,
) -> None:
    """
    Execute the decompress command to decompress a file.

    This function runs the ZStrong CLI's decompress command to decompress the
    specified file and save the result to the provided path.

    File paths in the test framework:
    - Compressed file: {output_dir_path}/compressed/{filename}.zl
    - Decompressed file: {output_dir_path}/decompressed/{filename}.{extension}

    Args:
        compressed_file_path: Path to the compressed file
        decompressed_file_path: Path where the decompressed file will be saved

    Raises:
        ValueError: If the decompression fails or the decompressed file is not created
    """
    decompress_args = (
        f"decompress {str(compressed_file_path)} -o {str(decompressed_file_path)}"
    )

    if execute_command(decompress_args) != 0:
        raise ValueError("Executing decompress command failed")

    if not os.path.exists(decompressed_file_path):
        raise ValueError("Decompressed file was not created")

    print(f"Decompressed {compressed_file_path} saved to {decompressed_file_path}")


def execute_train(
    compressor_info: CompressorInfo,
    uncompressed_dir: str,
    trained_compressor_path: str,
    trainer_name: str | None = None,
):
    """
    Execute the train command to train a compressor on sample files.

    This function runs the OpenZL CLI's train command to train a compressor
    on the sample files in the provided directory and save the trained compressor
    to the provided path.

    File paths in the test framework:
    - Sample directory: Directory containing files to use for training
    - Trained compressor: Path where the trained compressor will be saved

    Args:
        compressor_info: Configuration for the compressor to use for training
        uncompressed_dir: Directory containing sample files to use for training
        trained_compressor_path: Path where the trained compressor will be saved
        trainer_name: Name of the trainer algorithm to use (optional).
            Available options include:
            - "greedy": Makes locally optimal choices at each step
            - "full-split": Analyzes the entire dataset before making decisions
            If None, the default trainer for the profile will be used.

    Raises:
        ValueError: If the training fails or the trained compressor is not created
    """
    cstr = compressor_info.compressor_str
    cflag = compressor_info.compressor_type.value

    train_args = f"train --max-time-secs 1 --{cflag} {cstr} {str(uncompressed_dir)} -o {str(trained_compressor_path)}"
    if trainer_name:
        train_args += f" -t {trainer_name}"

    if execute_command(train_args) != 0:
        raise ValueError("Executing train command failed")

    if not os.path.exists(trained_compressor_path):
        raise ValueError("Trained compressor was not saved")

    print(
        f"Training {cflag} {cstr} succeeded on files in {uncompressed_dir} and trained compressor saved to {trained_compressor_path}"
    )


def execute_train_inline(
    compressor_info: CompressorInfo,
    uncompressed_file: str,
    compressed_file_path: str,
):
    cstr = compressor_info.compressor_str
    cflag = compressor_info.compressor_type.value

    train_args = f"compress --train-inline --train-inline-test-limit 1 --{cflag} {cstr} {str(uncompressed_file)} -o {str(compressed_file_path)}"

    if execute_command(train_args) != 0:
        raise ValueError("Executing train command failed")

    print(
        f"""Training {cflag} {cstr} succeeded on {uncompressed_file}.
        Compressed file saved to {compressed_file_path}"""
    )
