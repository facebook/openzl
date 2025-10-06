# Copyright (c) Meta Platforms, Inc. and affiliates.

import argparse
import sys
from typing import Dict

from .base_dataset_builder import BaseDatasetBuilder
from .dataset_builders import (
    BinanceDatasetBuilder,
    ERA5DatasetBuilder,
    # PPMFDatasetBuilder,
    # PSAMDatasetBuilder,
    REA6DatasetBuilder,
    TLCDatasetBuilder,
)


class DatasetManager:
    """Central manager for dataset downloading and catalog generation"""

    def __init__(self):
        # Commented out for now since census website is down
        self.available_datasets: Dict[str, BaseDatasetBuilder] = {
            "binance": BinanceDatasetBuilder(),
            "era5": ERA5DatasetBuilder(),
            # "ppmf": PPMFDatasetBuilder(),
            # "psam": PSAMDatasetBuilder(),
            "rea6": REA6DatasetBuilder(),
            "tlc": TLCDatasetBuilder(),
        }

    def list_datasets(self):
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


def create_argParser():
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


def main():
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
                failed_downloads = []
                for name, dataset in manager.available_datasets.items():
                    if not dataset.download(args.output_dir):
                        failed_downloads.append(name)
                print("Summary of downloads:")
                for fd in failed_downloads:
                    print(f"Failed to download {fd}")
                if len(failed_downloads) == 0:
                    print("All datasets downloaded successfully")

            else:
                if args.dataset_name in manager.available_datasets:
                    manager.available_datasets[args.dataset_name].download(
                        args.output_dir,
                        binary_path=args.binary_path,
                    )
                else:
                    print(
                        f"Dataset {args.dataset_name} not found. Use 'list' to see available datasets."
                    )
        else:
            manager.list_datasets()

    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
