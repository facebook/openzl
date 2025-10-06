# Copyright (c) Meta Platforms, Inc. and affiliates.

import os

from ...base_dataset_builder import BaseDatasetBuilder


class PSAMDatasetBuilder(BaseDatasetBuilder):
    """Dataset builder for census data on individual people and housing units"""

    def __init__(self) -> None:
        current_dir = os.path.dirname(os.path.abspath(__file__))
        manifest_path = os.path.join(current_dir, "psam_manifest.json")
        super().__init__(manifest_path)

    def download(
        self,
        download_dir: str,
        **kwargs,
    ) -> bool:
        """Download PSAM dataset to specified directory

        Args:
            download_dir: Directory to download files to
            **kwargs: Additional arguments

        Returns:
            bool: True if download successful, False otherwise
        """
        urls = self.manifest_data["download_config"]["urls"]
        csv_h_urls = [url for url in urls if "csv_h" in url]
        csv_p_urls = [url for url in urls if "csv_h" not in url]

        if not self.download_utils.download_files(
            csv_h_urls,
            os.path.join(download_dir, f"{self.name}_h"),
        ):
            print("Failed to download PSAM housing dataset")
            return False
        if not self.download_utils.download_files(
            csv_p_urls, os.path.join(download_dir, f"{self.name}_p")
        ):
            print("Failed to download PSAM people dataset")
            return False
        print("Successfully downloaded PSAM dataset")
        return True
