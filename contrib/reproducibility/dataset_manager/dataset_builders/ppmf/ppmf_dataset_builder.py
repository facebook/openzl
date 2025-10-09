# Copyright (c) Meta Platforms, Inc. and affiliates.

import os

from ...base_dataset_builder import BaseDatasetBuilder


class PPMFDatasetBuilder(BaseDatasetBuilder):
    """Dataset builder for census data on individual people and housing units (Successor to PSAM (PUMS) dataset)"""

    def __init__(self) -> None:
        current_dir = os.path.dirname(os.path.abspath(__file__))
        manifest_path = os.path.join(current_dir, "ppmf_manifest.json")
        super().__init__(manifest_path)

    def download(
        self,
        download_dir: str,
        **kwargs,
    ) -> bool:
        """Download PPMF dataset to specified directory

        Args:
            download_dir: Directory to download files to
            **kwargs: Additional arguments

        Returns:
            bool: True if download successful, False otherwise
        """

        if not self.download_utils.download_files(
            self.manifest_data["download_config"]["urls"],
            os.path.join(download_dir, self.name),
        ):
            print("Failed to download PPMF dataset")
            return False

        if  not self.post_download_processing(download_dir, kwargs):
            print("Failed to post-process PPMF dataset")
            return False

        print("Successfully downloaded and post-processed PPMF dataset")
        return True

    def post_download_processing(self, parent_dir: str, kwargs: dict) -> bool:
        """Chunk files

        Args:
            download_dir: Directory where files were downloaded
            **kwargs: Additional arguments

        Returns:
            bool: True if post-processing successful, False otherwise
        """
        download_dir = os.path.join(parent_dir, self.name)
        for file in os.listdir(download_dir):
            print(f"Beginning post processing for file: {file}")
            if not file.endswith(".csv"):
                continue

            base_name = os.path.splitext(file)[0]
            if base_name not in self.manifest_data["download_config"]["chunk_size"]:
                print(f"Chunk size not found for file: {file}")
                continue

            new_download_dir = os.path.join(
                parent_dir, file.replace(".csv", "").replace("2020_", "")
            )

            os.makedirs(
                new_download_dir,
                exist_ok=True,
            )

            os.rename(
                os.path.join(download_dir, file),
                os.path.join(new_download_dir, file.replace("2020_ppmf_", "")),
            )

            chunk_size = self.manifest_data["download_config"]["chunk_size"][base_name]
            if not self.download_utils.chunk_csv_file(
                file.replace("2020_ppmf_", ""), new_download_dir, chunk_size
            ):
                return False
        os.rmdir(download_dir)
        return True
