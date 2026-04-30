# Copyright (c) Meta Platforms, Inc. and affiliates.
import json
import os
from abc import ABC, abstractmethod

from .dataset_utils import DownloadUtils


class BaseDatasetBuilder(ABC):
    """Base class for all dataset builders"""

    def __init__(self, manifest_path: str | None = None) -> None:
        self.download_utils = DownloadUtils()
        self.name = ""
        self.manifest_data = {}

        if manifest_path:
            self._parse_manifest(manifest_path)

    def _parse_manifest(self, manifest_path: str | None = None) -> None:
        """Parse the manifest file and set dataset metadata"""
        if not os.path.exists(manifest_path):
            raise FileNotFoundError(f"Dataset manifest file not found: {manifest_path}")

        with open(manifest_path, "r") as f:
            self.manifest_data = json.load(f)

        self.name = self.manifest_data["name"]

    @abstractmethod
    def download(
        self,
        download_dir: str,
        **kwargs,
    ) -> bool:
        """Download dataset to specified directory"""
        pass

    def post_download_processing(self, download_dir: str, kwargs: dict) -> bool:
        """Override this for custom post-processing"""
        return True
