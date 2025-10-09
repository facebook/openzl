# Copyright (c) Meta Platforms, Inc. and affiliates.
import bz2
import os
import shutil
import subprocess
import zipfile
from pathlib import Path
from typing import Any, Dict, List, Optional
from urllib.parse import urlparse

import cdsapi
import pandas as pd
import requests
from kaggle.api.kaggle_api_extended import KaggleApi


# Doing this since gdal has extra installation steps
gdal_installed = True
try:
    from osgeo import gdal
except ImportError:
    gdal_installed = False

class DownloadUtils:
    """Shared download utilities for all datasets"""

    def __init__(self) -> None:
        pass

    @staticmethod
    def download_file_from_cds(
        dataset: str,
        request: Dict[str, Any],
        output_dir: str,
        simplified_names: Optional[List[str]] = None,
        max_bands: Optional[int] = None,
    ) -> bool:
        """Download a single file from Climate Data Store (CDS)"""
        try:
            output_path = os.path.join(
                output_dir, dataset + "." + request["data_format"]
            )
            client = cdsapi.Client()
            client.retrieve(dataset, request, target=output_path)

            if output_path.endswith("grib"):
                if DownloadUtils.grib_to_bin(
                    output_path,
                    (
                        simplified_names
                        if simplified_names is not None
                        else request["variable"]
                    ),
                    max_bands,
                ):
                    os.remove(output_path)
            else:
                return False
        except Exception as e:
            if "incomplete configuration file" in str(e):
                print(f"Please make sure to set up CDS api key - {e}")
            else:
                print(f"Unexpected Error: {e}")
                print(
                    "Error most likely from Climate Data Store, please try again or download directly from CDS"
                )

            return False

        return True

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
            api = KaggleApi()
            api.authenticate()

            for file in files:
                print(f"Downloading {kaggle_slug} - {file}")

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
    def download_file_from_url(
        url: str,
        output_path: str,
        chunk_size: int = 8192,
    ) -> bool:
        """Download a single file from URL"""
        try:
            response = requests.get(url, stream=True)
            response.raise_for_status()

            # Get total file size from headers
            total_size = int(response.headers.get("content-length", 0))

            with open(output_path, "wb") as f:
                downloaded = 0

                for chunk in response.iter_content(chunk_size=chunk_size):
                    if chunk:
                        f.write(chunk)
                        downloaded += len(chunk)

                        # Print progress
                        if total_size > 0:
                            percent = (downloaded / total_size) * 100
                            print(
                                f"\rDownloading {os.path.basename(output_path)}: {percent:.1f}% "
                                f"({downloaded:,} / {total_size:,} bytes)",
                                end="",
                                flush=True,
                            )
                        else:
                            print(
                                f"\rDownloading {os.path.basename(output_path)}: {downloaded:,} bytes",
                                end="",
                                flush=True,
                            )

            print(" - Finished Downloading")
            return True

        except Exception as e:
            print(f"Failed to download {url}: {e}")
            return False

    @staticmethod
    def download_files(
        file_urls: List[str],
        output_dir: str,
        alt_file_names: Optional[List[str]] = None,
    ) -> bool:
        """Download multiple files
        Args:
            file_urls: List of file names
            download_dir: Directory to download files to
            alt_file_names: Optional list of alternative file names to use for each file.
        """
        success = True

        # Make sure directory exists
        os.makedirs(output_dir, exist_ok=True)
        for ind, url in enumerate(file_urls):

            # Extract filename from URL and create output path
            parsed_url = urlparse(url)
            alt_file_name = (
                alt_file_names[ind]
                if (alt_file_names is not None and ind < len(alt_file_names))
                else None
            )
            filename = (
                os.path.basename(parsed_url.path)
                if alt_file_name is None
                else alt_file_name
            )
            output_path = os.path.join(output_dir, filename)

            # Download the file
            if not DownloadUtils.download_file_from_url(
                url,
                output_path,
            ):
                success = False

            if not zipfile.is_zipfile(output_path) and output_path.endswith(".zip"):
                print("Zip file is malformed - most likely issue with download link")
                success = False

            if DownloadUtils.extract_data(output_path, output_dir):
                os.remove(output_path)
            else:
                success = False

        return success

    @staticmethod
    def find_openzl_root(max_levels: int = 5) -> Optional[Path]:
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

                    openzl_root = DownloadUtils.find_openzl_root()
                    if openzl_root is None and binary_path is None:
                        print("make_canonical_parquet binary not found")
                        return False

                    bin_path = (
                        Path(binary_path)
                        if binary_path is not None
                        else os.path.join(
                            openzl_root,
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

    @staticmethod
    def extract_data(filepath: str, output_dir: str) -> bool:
        """Extract data from archive formats"""
        try:
            if zipfile.is_zipfile(filepath):
                print(f"Unzipping {os.path.basename(filepath)}")

                with zipfile.ZipFile(filepath, "r") as zip_ref:
                    zip_ref.extractall(output_dir)
            elif filepath.endswith(".grb.bz2"):
                print(f"Extracting bin from {os.path.basename(filepath)}")

                # decompress to .grb
                temp_grb_file = os.path.join(
                    # Move two files up since grib_to_bin creates nested folders
                    os.path.dirname(os.path.dirname(filepath)),
                    os.path.basename(filepath).replace(".bz2", ""),
                )

                with bz2.open(filepath, "rb") as f_in, open(
                    temp_grb_file, "wb"
                ) as f_out:
                    f_out.write(f_in.read())

                # convert to .bin
                result = DownloadUtils.grib_to_bin(
                    temp_grb_file, [os.path.basename(filepath.replace(".grb.bz2", ""))]
                )

                os.remove(temp_grb_file)

                if not result:
                    return False
            else:
                return False

            print(f"Extracted: {os.path.basename(filepath)}")
            return True

        except Exception as e:
            print(f"Failed to extract {filepath}: {e}")
            return False

    @staticmethod
    def grib_to_bin(
        grib_file_path: str,
        file_names: List[str],
        max_bands: Optional[int] = None,
    ) -> bool:
        """Convert GRIB file to binary format."""
        if not gdal_installed:
            print("Please install gdal in order to convert grib into bin file")
            return False
        try:
            dataset = gdal.Open(grib_file_path)
            num_bands = dataset.RasterCount
            xsize = dataset.RasterXSize
            ysize = dataset.RasterYSize

            gdal.UseExceptions()

            # Create directories
            base_dir = os.path.dirname(grib_file_path)
            dir_paths = [os.path.join(base_dir, kind) for kind in file_names]
            for dir_path in dir_paths:
                os.makedirs(dir_path, exist_ok=True)

            num_bands_to_process = (
                num_bands if max_bands is None else min(max_bands, num_bands) + 1
            )
            for i in range(1, num_bands_to_process):
                band = dataset.GetRasterBand(i)

                # Validate data type
                dt = band.DataType
                if dt != gdal.GDT_Float64:
                    print(
                        f"Warning: Band {i} data type is {gdal.GetDataTypeName(dt)}, expected Float64"
                    )

                # Validate dimensions
                if band.XSize != xsize or band.YSize != ysize:
                    print(f"Error: Band {i} dimensions don't match dataset")
                    return False

                raster_data = band.ReadRaster(
                    0,
                    0,  # x_offset, y_offset
                    xsize,
                    ysize,  # x_size, y_size
                    xsize,
                    ysize,  # buf_x_size, buf_y_size
                    gdal.GDT_Float64,  # data_type
                    0,
                    0,
                )

                if raster_data is None:
                    return False

                # Round-robin distribution
                which_kind_idx = (i - 1) % len(file_names)
                dir_path = dir_paths[which_kind_idx]
                out_path = os.path.join(
                    dir_path, f"{file_names[which_kind_idx]}_{i}.bin"
                )

                print(
                    f"\rWriting to {out_path}",
                    end="",
                    flush=True,
                )
                with open(out_path, "wb") as f:
                    f.write(raster_data)

            return True
        except Exception as e:
            print(f"Failed to convert GRIB to binary: {e}")
            return False

    @staticmethod
    def chunk_csv_file(file: str, download_dir: str, chunk_size: int) -> bool:
        """Chunk csv file and saves chunks into `download_dir`"""
        file_path = os.path.join(download_dir, file)
        base_name = os.path.splitext(file)[0]

        if not shutil.which("awk"):
            print(
                "awk not found. Using pandas to chunk CSV file - this may take a while"
            )
            chunk_num = 0
            for chunk in pd.read_csv(file_path, chunksize=chunk_size):
                output_file = os.path.join(download_dir, f"{base_name}_{chunk_num}.csv")
                chunk.to_csv(output_file, index=False)
                print(
                    f"\rProcessed chunk {chunk_num}",
                    end="",
                    flush=True,
                )
                chunk_num += 1
            return True

        try:
            print(f"Chunking {file} with awk...")
            # Using awk for fast CSV chunking
            awk_cmd = f"""
            cd "{download_dir}" &&
            awk -v chunk_size="{chunk_size}" -v base_name="{base_name}" -v output_dir="{download_dir}" '
            # Save the header line for the very first line
            NR == 1 {{
                header = $0;
                next
            }}
            # If we hit the file line of a new chunk create a new file,
            # add header and print out update
            (NR - 1) % chunk_size == 1 {{
                chunk_num = int((NR - 2) / chunk_size)
                filename = output_dir "/" base_name "_" chunk_num ".csv"
                print header > filename
                printf "\\rProcessing chunk %d...", chunk_num
                fflush()
            }}
            # Save the current line to the current file
            {{
                chunk_num = int((NR - 2) / chunk_size)
                filename = output_dir "/" base_name "_" chunk_num ".csv"
                print $0 >> filename
            }}
            END {{
                print "Chunking complete! Created " chunk_num + 1 " chunks"
            }}
            ' "{file_path}"
            """

            result = subprocess.run(
                awk_cmd, shell=True, capture_output=False, text=True
            )

            if result.returncode != 0:
                print(f"Awk command failed with return code: {result.returncode}")
                return False

            return True

        except Exception as e:
            print(f"Chunking error: {e}")
            return False
