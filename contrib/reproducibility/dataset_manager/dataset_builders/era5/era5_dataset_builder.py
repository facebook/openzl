# Copyright (c) Meta Platforms, Inc. and affiliates.

import os

from ...base_dataset_builder import BaseDatasetBuilder

data_variable_to_name = {
    "10m_u_component_of_wind": "ERA5_wind",
    "mean_sea_level_pressure": "ERA5_pressure",
    "total_precipitation": "ERA5_precip",
    "downward_uv_radiation_at_the_surface": "ERA5_flux",
    "snow_density": "ERA5_snow",
}


class ERA5DatasetBuilder(BaseDatasetBuilder):
    """Dataset builder for ERA5 climate data"""

    def __init__(self) -> None:
        current_dir = os.path.dirname(os.path.abspath(__file__))
        manifest_path = os.path.join(current_dir, "era5_manifest.json")
        super().__init__(manifest_path)

    def download(
        self,
        download_dir: str,
        **kwargs,
    ) -> bool:
        """Download ERA5 dataset to specified directory

        Args:
            download_dir: Directory to download files to
            **kwargs: Additional arguments

        Returns:
            bool: True if download successful, False otherwise
        """
        if self.download_utils.download_file_from_cds(
            self.manifest_data["download_config"]["dataset"],
            self.manifest_data["download_config"]["request"],
            download_dir,
            list(data_variable_to_name.values()),
            self.manifest_data["download_config"]["max_bands"],
        ):
            print("ERA5 dataset downloaded successfully")
            return True
        else:
            print("Failed to download ERA5 dataset")
            return False
