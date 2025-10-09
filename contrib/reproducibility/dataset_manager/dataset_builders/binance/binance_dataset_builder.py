# Copyright (c) Meta Platforms, Inc. and affiliates.

import os

from ...base_dataset_builder import BaseDatasetBuilder


class BinanceDatasetBuilder(BaseDatasetBuilder):
    """Dataset builder for cryptocurrency pairs data on Binance.com"""

    def __init__(self) -> None:
        current_dir = os.path.dirname(os.path.abspath(__file__))
        manifest_path = os.path.join(current_dir, "binance_manifest.json")
        super().__init__(manifest_path)

    def download(
        self,
        download_dir: str,
        **kwargs,
    ) -> bool:
        """Download Binance dataset to specified directory

        Args:
            download_dir: Directory to download files to
            **kwargs: Additional arguments

        Returns:
            bool: True if download successful, False otherwise
        """
        if not self.download_utils.download_file_from_kaggle(
            self.manifest_data["download_config"]["kaggle_slug"],
            self.manifest_data["download_config"]["kaggle_files"],
            download_dir,
            self.name,
        ):
            print("Failed to download Binance dataset")
            return False

        if not self.post_download_processing(download_dir, kwargs):
            print("Failed to post-process Binance dataset")
            return False

        print("Binance dataset downloaded and post processed successfully")
        return True

    def post_download_processing(self, download_dir: str, kwargs: dict) -> bool:
        """Verify parquet files are "canonical" (i.e no compression, no encoding)

        Args:
            download_dir: Directory where files were downloaded
            **kwargs: Additional arguments

        Returns:
            bool: True if post-processing successful, False otherwise
        """
        if self.download_utils.verify_parquet_canonical(
            os.path.join(download_dir, self.name),
            os.path.join(download_dir, f"{self.name}_canonical"),
            kwargs.get("binary_path"),
        ):
            print("Binance dataset post-processing successful")
            return True
        else:
            return False
