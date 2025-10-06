# Copyright (c) Meta Platforms, Inc. and affiliates.
import os
import shutil
import subprocess
from pathlib import Path
from typing import List, Optional

from kaggle.api.kaggle_api_extended import KaggleApi

class DownloadUtils:
    """Shared download utilities for all datasets"""

    def __init__(self) -> None:
        pass

    @staticmethod
    def download_file_from_kaggle(
        kaggle_slug: str,
        files: List[str],
        output_path: str,
        dataset_name: str = None,
    ) -> bool:
        """Download a single file from Kaggle"""
        try:

            dataset_dir = os.path.join(output_path, kaggle_slug.split("/")[-1])

            for file in files:
                print(f"Downloading {kaggle_slug} - {file}")
                api = KaggleApi()
                api.authenticate()

                api.dataset_download_file(kaggle_slug, file, path=dataset_dir)

                if DownloadUtils.extract_data(
                    os.path.join(dataset_dir, file),
                    os.path.join(output_path, dataset_name),
                ):
                    os.remove(os.path.join(dataset_dir, file))
                else:
                    os.remove(os.path.join(dataset_dir, file))
                    os.rmdir(dataset_dir)
                    return False

            os.rmdir(dataset_dir)
            print(f"Finished Downloading: {os.path.basename(dataset_dir)}")
            return True
        except Exception as e:
            if "Unauthorized" in str(e):
                print(e)
                print("kaggle.json API key is not set up correctly")
                return False
            print(f"Unexpected error: {e}")
            return False


    @staticmethod
    def find_openzl_root(max_levels: int = 10) -> Optional[Path]:
        """Get the path to the binary file"""
        current_dir = Path(__file__).parent
        for _ in range(max_levels):
            if current_dir.name == "openzl":
                openzl_root = current_dir
                return openzl_root

            # Go up one level
            parent = current_dir.parent
            if parent == current_dir:
                # Hit filesystem root - stop searching
                break
            current_dir = parent
        return None

    @staticmethod
    def verify_parquet_canonical(
        input_dir: str, output_dir: str, binary_path: Optional[str] = None
    ) -> bool:

        try:
            files = os.listdir(input_dir)
            os.makedirs(output_dir, exist_ok=True)

            for file in files:
                if file.endswith(".parquet"):
                    print(f"Verifying and converting {os.path.join(input_dir, file)}")

                    input_file_path = os.path.join(input_dir, file)
                    output_file_path = os.path.join(output_dir, file)

                    bin_path = (
                        Path(binary_path)
                        if binary_path is not None
                        else os.path.join(
                            DownloadUtils.find_openzl_root(),
                            "cmakebuild/tools/parquet/make_canonical_parquet",
                        )
                    )
                    # Temporarily copy original parquet file to output dir
                    shutil.copy2(input_file_path, output_file_path)

                    # Turn parquet file canonical
                    subprocess.run(
                        [
                            str(
                                bin_path,
                            ),
                            "--input",
                            output_file_path,
                        ],
                        capture_output=False,
                        text=True,
                    )

                    # Remove non canonical parquet file (canonical version is saved with .canonical)
                    os.remove(output_file_path)

            return True
        except Exception as e:
            print(f"Failed to verify and convert parquet file to canonical: {e}")
            return False
