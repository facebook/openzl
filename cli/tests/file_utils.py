# Copyright (c) Meta Platforms, Inc. and affiliates.

import os
import tempfile
from dataclasses import dataclass

# Base directory for all temporary test files
TEMP_DIR_PREFIX = os.path.join(tempfile.gettempdir(), "zli_integration_test")

# Directory structure constants
SAMPLE_FILES_DIR = "sample_files"  # read-only input in local test dir
COMPRESSED_DIR = "compressed"  # temp output under /tmp/
DECOMPRESSED_DIR = "decompressed"  # temp output under /tmp/


def read_file_contents(path: str) -> bytes:
    """
    Read the binary contents of a file.
    This is used to compare the original and decompressed files.

    Args:
        path: Path to the file to read

    Returns:
        Binary content of the file

    Raises:
        ValueError: If the file cannot be read
    """
    try:
        with open(path, "rb") as f:
            content = f.read()

        return content
    except IOError:
        raise ValueError(f"Failed to read file: {path}")


def file_contents_match(path1: str, path2: str) -> bool:
    """
    Check if two files have the same content.

    Args:
        path1: Path to the first file
        path2: Path to the second file

    Returns:
        True if the files have the same content, False otherwise
    """
    return read_file_contents(path1) == read_file_contents(path2)


def input_dir_path(input_dir_name: str) -> str:
    """
    Get the directory path for the test input files.

    The input files are located at:
    cli/tests/sample_files/{input_dir_name}/

    Args:
        input_dir_name: Directory name for input sample files

    Returns:
        Absolute path to the directory containing input files for this test
    """
    return os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        SAMPLE_FILES_DIR,
        input_dir_name,
    )


@dataclass(frozen=True)
class SampleFile:
    """
    Sample file for compression testing with paths and content management.

    This class manages the paths for original, compressed, and decompressed files
    used in compression tests. It provides properties to access these paths and
    to compare file contents.

    Directory structure:
    - Original files: cli/tests/sample_files/{input_dir_name}/
    - Compressed files: {output_dir_path}/compressed/
    - Decompressed files: {output_dir_path}/decompressed/

    Attributes:
        orig_file_path: Path to the original sample file
        output_dir_path: Path to the directory for test outputs
    """

    orig_file_path: str
    output_dir_path: str

    @property
    def input_dir_name(self) -> str:
        """
        Extract the input directory name from the original file path.

        Returns:
            The input_dir_name from path (.../sample_files/{input_dir_name}/filename)
        """
        return os.path.basename(os.path.dirname(self.orig_file_path))

    @property
    def compression_dir(self) -> str:
        """
        Get the directory path for compressed files.

        Returns:
            Path to {output_dir_path}/compressed/
        """
        return os.path.join(self.output_dir_path, COMPRESSED_DIR)

    @property
    def decompression_dir(self) -> str:
        """
        Get the directory path for decompressed files.

        Returns:
            Path to {output_dir_path}/decompressed/
        """
        return os.path.join(self.output_dir_path, DECOMPRESSED_DIR)

    @property
    def base_file_name(self) -> str:
        """
        Get the base file name without extension.

        Returns:
            Base filename without extension
        """
        return os.path.splitext(os.path.basename(self.orig_file_path))[0]

    @property
    def compressed_file_path(self) -> str:
        """
        Get the path to the compressed file.

        Returns:
            Path to {output_dir_path}/compressed/{base_file_name}.zl
        """
        path = os.path.join(self.compression_dir, f"{self.base_file_name}.zl")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        return path

    @property
    def decompressed_file_path(self) -> str:
        """
        Get the path to the decompressed file.

        Returns:
            Path to {output_dir_path}/decompressed/{base_file_name}.{original_extension}
        """
        _, extension = os.path.splitext(os.path.basename(self.orig_file_path))
        file_extension = extension[1:] if extension else ""

        path = os.path.join(
            self.decompression_dir,
            f"{self.base_file_name}.{file_extension}",
        )
        os.makedirs(os.path.dirname(path), exist_ok=True)
        return path

    @property
    def original_matches_decompressed(self) -> bool:
        """
        Check if the original file content matches the decompressed file content.

        Returns:
            True if the content of the original and decompressed files match
        """
        return file_contents_match(self.orig_file_path, self.decompressed_file_path)


def get_sample_files_from_dir(
    input_dir_name: str,
    output_dir_path: str,
) -> list[SampleFile]:
    """
    Get a list of sample files from a directory.

    This function finds all files in the directory:
    cli/tests/sample_files/{input_dir_name}/

    Args:
        input_dir_name: Directory name where sample files are located
            (corresponds to a subdirectory in sample_files/)
        output_dir_path: Path to the directory for test outputs

    Returns:
        A list of SampleFile objects representing the sample files in the directory.

    Raises:
        ValueError: If the directory does not exist or cannot be read
    """
    try:
        if not os.path.isdir(input_dir_path(input_dir_name)):
            raise ValueError(
                f"Directory does not exist: {input_dir_path(input_dir_name)}"
            )

        sample_files = []
        for sample_file_name in os.listdir(input_dir_path(input_dir_name)):
            full_file_path = os.path.join(
                input_dir_path(input_dir_name), sample_file_name
            )
            if not os.path.isfile(full_file_path):
                continue

            sample_files.append(
                SampleFile(
                    orig_file_path=full_file_path,
                    output_dir_path=output_dir_path,
                )
            )

        return sample_files

    except OSError as e:
        raise ValueError(
            f"Failed to read directory {input_dir_path(input_dir_name)}: {e}"
        )
