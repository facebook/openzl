# ZStrong CLI Test Framework

This directory contains integration tests for the ZStrong CLI compression tool.

## Directory Structure

```text
cli/tests/
├── abstract_compression_test.py  # Base test classes
├── cli_integration_tests.py      # Concrete test implementations
├── command_utils.py              # CLI command execution utilities
├── file_utils.py                 # File handling utilities
└── sample_files/                 # Test input files
    ├── csv/                      # Sample CSV files for CSV test
    │   └── output_neighbors.csv
    └── zstd/                     # Sample files for ZSTD test
        └── ...
```

## Test Directory Structure

Each test uses the following directory structure:

1. **Input Files**: Located at `cli/tests/sample_files/{input_dir_name}/`
   - These are the original files used for testing compression/decompression

2. **Temporary Test Files**: Created at a random temporary directory
   - Compressed files: `{output_dir_path}/compressed/{filename}.zl`
   - Decompressed files: `{output_dir_path}/decompressed/{filename}.{extension}`

## Adding New Tests

1. Create a new test class in `cli_integration_tests.py` that inherits from one of:
   - `_CompressDecompressBaseTest`: For basic compression/decompression tests
   - `_TrainBaseTest`: For tests that include training a compressor

2. Implement the required properties:
   - For all test classes:
     - `input_dir_name`: Directory name for input sample files (e.g., "csv", "zstd")

   - For `_CompressDecompressBaseTest` subclasses:
     - `compressor_info`: Configuration for the compressor to use

   - For `_TrainBaseTest` subclasses:
     - `compressor_profile_name`: Profile name to use for training

   - Optional properties:
     - `trainer_name`: Custom trainer algorithm (for _TrainBaseTest subclasses)

3. Add sample files in `cli/tests/sample_files/{input_dir_name}/`

4. Implement the required test methods:
   - For `_CompressDecompressBaseTest`: `test_compress_decompress()`
   - For `_TrainBaseTest`: `test_train_compress_decompress()`

## Examples

### Basic Compression Test

```python
class NewFormatTest(_CompressDecompressBaseTest):
    """Test case for a new file format"""

    @property
    def input_dir_name(self) -> str:
        return "new_format"  # Sample files should be in sample_files/new_format/

    @property
    def compressor_info(self) -> CompressorInfo:
        return CompressorInfo(
            compressor_str="new_format",
            compressor_type=CompressorType.PROFILE,
        )

    def test_compress_decompress(self):
        self.compress_and_decompress_samples()
```

### Training-Based Test

```python
class NewFormatTrainTest(_TrainBaseTest):
    """Test case for a new file format with training"""

    @property
    def input_dir_name(self) -> str:
        return "new_format"  # Sample files should be in sample_files/new_format/

    @property
    def compressor_profile_name(self) -> str:
        return "new_format"  # Profile name to use for training

    @property
    def trainer_name(self) -> str | None:
        return "custom_trainer"  # Custom trainer algorithm (optional)

    def test_train_compress_decompress(self):
        self.train_compress_decompress()
```

## Running Tests

All tests can be run using the following command:
(Note – this gives a single pass/fail result)

```bash
buck2 test cli/tests:all_integration_tests
```

Specific tests can be run using the following commands referencing the individual targets:
```
buck2 test cli/tests:zstd_test
buck2 test cli/tests:csv_default_test
buck2 test cli/tests:csv_greedy_test
buck2 test cli/tests:csv_full_split_test
buck2 test cli/tests:csv_train_inline_test
buck2 test cli/tests:parquet_test


```

To get the stdout, use buck2 run instead of buck2 test, eg
```
buck2 run cli/tests:all_integration_tests
```
