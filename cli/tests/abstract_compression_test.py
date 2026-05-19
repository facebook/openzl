# Copyright (c) Meta Platforms, Inc. and affiliates.

import os
import shutil
import tempfile
import unittest

from command_utils import (
    CompressorInfo,
    CompressorType,
    execute_benchmark,
    execute_compress,
    execute_decompress,
    execute_train,
    execute_train_inline,
)
from file_utils import (
    file_contents_match,
    get_sample_files_from_dir,
    input_dir_path,
    SampleFile,
)

try:
    import openzl.ext as _zl  # noqa: F401

    HAS_OPENZL_EXT = True
except ImportError:
    HAS_OPENZL_EXT = False


class _CompressDecompressBaseTest(unittest.TestCase):
    """
    Integration test for the zli command-line interface - Abstract base class

    This class provides the base functionality for compression/decompression tests.
    It handles test directory setup/cleanup and provides methods for compressing
    and decompressing sample files.

    Directory Structure:
    - Input files: cli/tests/sample_files/{input_dir_name}/
    - Test output directory: {output_dir_path}
    - Compressed files: {output_dir_path}/compressed/
    - Decompressed files: {output_dir_path}/decompressed/
    """

    @property
    def compressor_profile_name(self) -> str:
        """
        Return the profile name to use for compression/training.
        """
        raise NotImplementedError("Subclasses must implement compressor_profile_name")

    @property
    def input_dir_name(self) -> str:
        """
        Return the directory name for input sample files.

        This property determines where sample files are located:
        cli/tests/sample_files/{input_dir_name}/
        """
        raise NotImplementedError("Subclasses must implement input_dir_name")

    @property
    def compressor_info(self) -> CompressorInfo:
        """
        Return the compressor configuration for this test.

        # TODO currently all non-training tests use CompressorType.PROFILE,
        # but we'll eventually want to add support for CompressorType.FILE

        Returns:
            CompressorInfo with the profile name set to compressor_profile_name
        """
        return CompressorInfo(
            compressor_str=self.compressor_profile_name,
            compressor_type=CompressorType.PROFILE,
        )

    @property
    def extra_args(self) -> str | None:
        """
        Return any extra arguments to pass to the CLI.

        This property is used to pass additional arguments to the CLI when
        compressing and decompressing files. It defaults to an empty string,
        but subclasses can override it to add custom arguments.

        Returns:
            An empty string by default, but subclasses can override it
        """
        return None

    def setUp(self) -> None:
        """
        Create and add cleanup for the test directory.

        This method:
        1. Creates a clean test output directory at {output_dir_path}
        """
        self.output_dir_path = tempfile.mkdtemp()
        if os.path.exists(self.output_dir_path):
            shutil.rmtree(self.output_dir_path)

        self.addCleanup(lambda: shutil.rmtree(self.output_dir_path, True))

        # Create the test directory
        os.makedirs(self.output_dir_path)

    @property
    def input_samples(self) -> list[SampleFile]:
        """
        Get the sample files for this test.

        Returns a list of SampleFile objects representing the files in:
        cli/tests/sample_files/{input_dir_name}/

        Returns:
            List of SampleFile objects for testing
        """
        return get_sample_files_from_dir(self.input_dir_name, self.output_dir_path)

    def compress_and_decompress_samples(self) -> None:
        """
        Compress and decompress all sample files and verify results.

        This method:
        1. Compresses each sample file to {output_dir_path}/compressed/
        2. Decompresses each file to {output_dir_path}/decompressed/
        3. Verifies that the decompressed file matches the original

        Raises:
            ValueError: If decompressed file doesn't match the original.
        """
        print(
            f"""Compressing and decompressing {len(self.input_samples)} samples
            using {self.compressor_info.compressor_type.value} {self.compressor_info.compressor_str} {" with extra args " + self.extra_args if self.extra_args is not None else ""}"""
        )

        for sample in self.input_samples:
            execute_compress(
                file_to_compress_path=sample.orig_file_path,
                compressor_info=self.compressor_info,
                compressed_file_path=sample.compressed_file_path,
                extra_args=self.extra_args,
            )

            execute_decompress(
                compressed_file_path=sample.compressed_file_path,
                decompressed_file_path=sample.decompressed_file_path,
            )

            if not sample.original_matches_decompressed:
                raise ValueError(
                    f"Decompressed file does not match original file: {sample.orig_file_path}"
                )

        print(
            f"All {len(self.input_samples)} samples were compressed and decompressed successfully."
        )


class _TrainBaseTest(_CompressDecompressBaseTest):
    """
    Test case for compression with training

    This class extends _CompressDecompressBaseTest to add training functionality.
    It handles the workflow of:
    1. Training a compressor on sample files
    2. Saving the trained compressor to {output_dir_path}/trained_compressor.zlc
    3. Using the trained compressor to compress and decompress files
    """

    @property
    def training_compressor_info(self) -> CompressorInfo:
        """
        Return the compressor configuration for training.

        This is used when training the compressor with the CLI.

        Returns:
            CompressorInfo with the profile name set to the test's profile name
        """
        return CompressorInfo(
            compressor_str=self.compressor_profile_name,
            compressor_type=CompressorType.PROFILE,
        )

    @property
    def compressor_info(self) -> CompressorInfo:
        """
        Path to the trained compressor file.

        The trained compressor is saved at:
        {output_dir_path}/trained_compressor.zlc

        Returns:
            CompressorInfo with the file path to the trained compressor
        """
        return CompressorInfo(
            compressor_str=os.path.join(self.output_dir_path, "trained_compressor.zlc"),
            compressor_type=CompressorType.FILE,
        )

    @property
    def trainer_name(self) -> str | None:
        """
        Return the trainer name to use for training.

        By default, this is None, which means the default trainer will be used.
        Subclasses can override this to use a specific trainer.
        """
        return None

    def train_compress_decompress(self) -> None:
        """
        Test the full workflow of training, compressing, and decompressing.

        This method:
        1. Trains a compressor on the sample files using the specified trainer (self.trainer_name)
        2. Saves the trained compressor to {output_dir_path}/trained_compressor.zlc
        3. Uses the trained compressor to compress and decompress the sample files
        4. Verifies that the decompressed files match the originals

        The trainer_name property determines which training algorithm is used (e.g., "greedy", "full-split").

        Raises:
            ValueError: If no input samples are found or if decompression fails
        """
        if not self.input_samples:
            raise ValueError("No input samples found for training")

        execute_train(
            compressor_info=self.training_compressor_info,
            uncompressed_dir=input_dir_path(self.input_dir_name),
            trained_compressor_path=self.compressor_info.compressor_str,
            trainer_name=self.trainer_name,
            extra_args=self.extra_args,
        )

        # Compress and decompress using the trained compressor
        self.compress_and_decompress_samples()


class _MLBaseTest(_TrainBaseTest):
    """
    Abstract base class for ML compression tests with training.
    """

    def setUp(self) -> None:
        super().setUp()
        # Create the serialized compressors folder once per test
        self.serialized_compressors_folder = self.get_serialized_compressors()

    def get_serialized_compressors(self) -> str:
        """
        Generate serialized compressors using static successors from numeric-ml-selector-64 profile.

        Returns:
            str: Path to the temporary folder containing the serialized .cbor files
        """
        import openzl.ext as zl

        def field_lz() -> bytes:
            compressor = zl.Compressor()
            graph = zl.graphs.FieldLz()(compressor)
            compressor.select_starting_graph(graph)
            return compressor.serialize()

        def delta_field_lz() -> bytes:
            compressor = zl.Compressor()
            graph = zl.graphs.FieldLz()(compressor)
            graph = zl.nodes.DeltaInt()(compressor, graph)
            compressor.select_starting_graph(graph)
            return compressor.serialize()

        def range_pack() -> bytes:
            compressor = zl.Compressor()
            graph = zl.nodes.RangePack()(compressor, successor=zl.graphs.FieldLz())
            compressor.select_starting_graph(graph)
            return compressor.serialize()

        def range_pack_zstd() -> bytes:
            compressor = zl.Compressor()
            graph = zl.nodes.RangePack()(compressor, successor=zl.graphs.Zstd())
            compressor.select_starting_graph(graph)
            return compressor.serialize()

        def tokenize_delta_fieldlz() -> bytes:
            compressor = zl.Compressor()
            delta_fieldlz = zl.nodes.DeltaInt()(compressor, zl.graphs.FieldLz())
            tokenize = zl.nodes.Tokenize(type=zl.Type.Numeric, sort=True)
            graph = tokenize(
                compressor,
                alphabet=delta_fieldlz,
                indices=zl.graphs.FieldLz(),
            )
            compressor.select_starting_graph(graph)
            return compressor.serialize()

        def zstd() -> bytes:
            compressor = zl.Compressor()
            graph = zl.graphs.Zstd()(compressor)
            compressor.select_starting_graph(graph)
            return compressor.serialize()

        compressors = {
            "00_field_lz": field_lz(),
            "01_range_pack": range_pack(),
            "02_range_pack_zstd": range_pack_zstd(),
            "03_delta_field_lz": delta_field_lz(),
            "04_tokenize_delta_fieldlz": tokenize_delta_fieldlz(),
            "05_zstd": zstd(),
        }

        # Create a temporary directory for the serialized compressors
        compressor_dir = tempfile.mkdtemp()
        self.addCleanup(lambda: shutil.rmtree(compressor_dir, True))

        for name, data in compressors.items():
            with open(os.path.join(compressor_dir, f"{name}.cbor"), "wb") as f:
                f.write(data)

        return compressor_dir

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


class _CsvBaseTest(_TrainBaseTest):
    """
    Abstract base class for CSV compression tests with training.

    This class extends _TrainBaseTest to specifically pull CSV files from sample_files/csv/.
    It is meant to be subclassed with specific trainer implementations (e.g., greedy, full-split).
    """

    @property
    def input_dir_name(self) -> str:
        """
        Return the directory name for input sample files.

        This property determines where sample files are located:
        cli/tests/sample_files/csv/

        Returns:
            "csv" as the input directory name
        """
        return "csv"

    @property
    def compressor_profile_name(self) -> str:
        """
        Return the profile name to use for compression/training.

        Returns:
            "csv" as the profile name
        """
        return "csv"


class _TrainInlineBaseTest(_TrainBaseTest):
    @property
    def input_file_name(self) -> str:
        raise NotImplementedError("Subclasses must implement input_file_name")

    @property
    def input_dir_name(self) -> str:
        return "/".join(self.input_file_name.split("/")[:-1])

    def train_inline(self) -> None:
        """
        Test the train-inline workflow for a single file.

        This method:
        1. Trains a compressor on a single file and compresses it in one step
           using the train-inline command
        2. Saves the trained compressor to {output_dir_path}/trained_compressor.zlc
        3. Decompresses the compressed file
        4. Verifies that the decompressed file matches the original

        The trainer_name property determines which training algorithm is used (e.g., "greedy", "full-split").

        Raises:
            ValueError: If the input file is not found or if decompression fails
        """

        sample_trained = SampleFile(
            orig_file_path=input_dir_path(self.input_file_name),
            output_dir_path=self.output_dir_path + "/trained",
        )

        sample_untrained = SampleFile(
            orig_file_path=input_dir_path(self.input_file_name),
            output_dir_path=self.output_dir_path + "/untrained",
        )

        # Train and compress in one step
        execute_train_inline(
            compressor_info=self.training_compressor_info,
            uncompressed_file=sample_trained.orig_file_path,
            compressed_file_path=sample_trained.compressed_file_path,
        )

        # Compress without training
        execute_compress(
            file_to_compress_path=sample_untrained.orig_file_path,
            compressor_info=self.training_compressor_info,
            compressed_file_path=sample_untrained.compressed_file_path,
            extra_args=self.extra_args,
        )

        if file_contents_match(
            sample_trained.compressed_file_path, sample_untrained.compressed_file_path
        ):
            raise ValueError(
                "Compressed file from train-inline matched file compressed with the untrained compressor"
            )

        print(
            "Verified that the trained compressor produces a different file compared to train-inline"
        )

        execute_decompress(
            compressed_file_path=sample_trained.compressed_file_path,
            decompressed_file_path=sample_trained.decompressed_file_path,
        )

        if not sample_trained.original_matches_decompressed:
            raise ValueError(
                f"Decompressed file does not match original file: {sample_trained.orig_file_path}"
            )

        print("Verified that the decompressed file matches the original file")


class _BenchmarkBaseTest(_CompressDecompressBaseTest):
    def benchmark(self) -> None:
        """
        Tests the benchmark workflow for a single file.

        This method benchmarks the provided input samples. Each benchmark does the following:
        1. Compresses each sample file n times and records benchmark results
        2. Decompresses each file n times and records benchmark results
        3. Verifies that the decompressed file matches the original
        """
        print(
            f"""Benchmarking {len(self.input_samples)} samples
            using {self.compressor_info.compressor_type.value} {self.compressor_info.compressor_str} {" with extra args " + self.extra_args if self.extra_args is not None else ""}"""
        )

        for sample in self.input_samples:
            execute_benchmark(
                file_to_compress_path=sample.orig_file_path,
                compressor_info=self.compressor_info,
                extra_args=self.extra_args,
            )
