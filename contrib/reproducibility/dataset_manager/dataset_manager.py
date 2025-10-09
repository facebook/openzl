# Copyright (c) Meta Platforms, Inc. and affiliates.
# pyre-strict

import argparse
import sys
from typing import Dict

from .base_dataset_builder import BaseDatasetBuilder
from .dataset_builders import (
    BinanceDatasetBuilder,
    ERA5DatasetBuilder,
    PPMFDatasetBuilder,
    PSAMDatasetBuilder,
)


class DatasetManager:
    """Central manager for dataset downloading and catalog generation"""

    def __init__(self) -> None:
        self.available_datasets: Dict[str, BaseDatasetBuilder] = {
            "binance": BinanceDatasetBuilder(),
            "era5": ERA5DatasetBuilder(),
            "ppmf": PPMFDatasetBuilder(),
            "psam": PSAMDatasetBuilder(),
        }

    def list_datasets(self) -> None:
        """List all available datasets"""
        print("Available datasets:")
        for k, v in self.available_datasets.items():
            print(f"  {k:<10} - {v.manifest_data['description']}")

    def generate_catalog(
        self, output_file: str = "catalog.yaml", include_stats: bool = True
    ) -> None:
        """Generate a comprehensive catalog of all available datasets"""
        # todo
        pass

    def download_all_datasets(self, output_dir: str, binary_path: str) -> None:
        """Download all datasets to output_dir"""
        failed_downloads = []
        for name, dataset in self.available_datasets.items():
            if not dataset.download(output_dir, binary_path=binary_path):
                failed_downloads.append(name)
        print("Summary of downloads:")
        for fd in failed_downloads:
            print(f"Failed to download {fd}")
        if len(failed_downloads) == 0:
            print("All datasets downloaded successfully")

    def download_dataset(self, output_dir: str, dataset_name: str, binary_path: str) -> None:
        """Download a single dataset to output_dir"""
        if dataset_name in self.available_datasets:
            self.available_datasets[dataset_name].download(
                output_dir,
                binary_path=binary_path,
            )
        else:
            print(
                f"Dataset {dataset_name} not found. Use 'list' to see available datasets."
            )


def create_argParser() -> argparse.ArgumentParser:
    """Create and configure argument parser"""

    parser = argparse.ArgumentParser(
        description="Download datasets",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    # Create subcommands
    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    # List command - no arguments needed
    subparsers.add_parser("list", help="List available datasets")

    # Download command - requires dataset_name and output_dir
    download_parser = subparsers.add_parser(
        "download", help="Download one or more datasets"
    )
    download_parser.add_argument(
        "-o", "--output-dir", dest="output_dir", required=True, help="Output directory"
    )

    group_parser = download_parser.add_mutually_exclusive_group(required=True)
    group_parser.add_argument(
        "-a", "--all", action="store_true", help="Download all datasets"
    )
    group_parser.add_argument(
        "-d", "--dataset", dest="dataset_name", help="Name of dataset to download"
    )

    download_parser.add_argument(
        "--binary-path",
        dest="binary_path",
        help="Path for binary file needed to convert parquet datasets to canonical",
        required=False,
    )
    return parser

def main() -> None:
    parser = create_argParser()
    args = parser.parse_args()

    # Show help if no command provided
    if not args.command:
        parser.print_help()
        sys.exit(1)

    manager = DatasetManager()

    try:
        if args.command == "download":
            if args.all:
                manager.download_all_datasets(args.output_dir, args.binary_path)
            else:
                manager.download_dataset(args.output_dir, args.dataset_name, args.binary_path)
        else:
            manager.list_datasets()

    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
