# Copyright (c) Meta Platforms, Inc. and affiliates.

import os

from ...base_dataset_builder import BaseDatasetBuilder


class REA6DatasetBuilder(BaseDatasetBuilder):
    """Dataset builder for data on regional climate change in the European continent"""

    def __init__(self) -> None:
        current_dir = os.path.dirname(os.path.abspath(__file__))
        manifest_path = os.path.join(current_dir, "rea6_manifest.json")
        super().__init__(manifest_path)

    def download(
        self,
        download_dir: str,
        **kwargs,
    ) -> bool:
        """Download REA6 dataset to specified directory

        Args:
            download_dir: Directory to download files to
            **kwargs: Additional arguments

        Returns:
            bool: True if download successful, False otherwise
        """
        if self.download_utils.download_files(
            self.manifest_data["download_config"]["urls"],
            os.path.join(download_dir, self.name),
            [
                self.name + ".grb.bz2"
            ],  # providing alternate name since original file name has . in it
        ):
            print("REA6 dataset downloaded successfully")
            return True
        else:
            print("Failed to download REA6 dataset")
            return False
