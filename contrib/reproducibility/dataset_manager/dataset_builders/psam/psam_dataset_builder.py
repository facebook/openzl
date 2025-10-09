# Copyright (c) Meta Platforms, Inc. and affiliates.

import os
import glob

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

        _h_folder = os.path.join(download_dir, f"{self.name}_h")
        _p_folder = os.path.join(download_dir, f"{self.name}_p")

        if not self.download_utils.download_files(
            csv_h_urls,
            _h_folder,
        ):
            print("Failed to download PSAM housing dataset")
            return False
        if not self.download_utils.download_files(
            csv_p_urls,
            _p_folder,
        ):
            print("Failed to download PSAM people dataset")
            return False

        self.post_download_processing(_h_folder, kwargs)
        self.post_download_processing(_p_folder, kwargs)

        print("Successfully downloaded PSAM dataset")
        return True

    def post_download_processing(self, download_dir: str, kwargs: dict) -> bool:
        """Remove psam readme pdf

        Args:
            download_dir: Directory where files were downloaded
            **kwargs: Additional arguments

        Returns:
            bool: True if post-processing successful, False otherwise
        """
        pdf_files = glob.glob(os.path.join(download_dir, "*.pdf"))

        # Delete each PDF file
        for pdf_file in pdf_files:
            os.remove(pdf_file)
            print(f"Remove extra PSAM pdf file: {pdf_file}")

        return True
