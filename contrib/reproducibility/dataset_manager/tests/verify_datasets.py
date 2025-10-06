# Copyright (c) Meta Platforms, Inc. and affiliates.

import hashlib
import os
import random
import subprocess
import tempfile
from io import BytesIO
from pathlib import Path
from typing import Optional

import pyarrow.parquet as pq
import zstandard as zstd


def get_file_sha(file_path: Path) -> str:
    """Calculate SHA256 hash of a local file."""
    with open(file_path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def download_manifold_file(manifold_path: str, corpus: str) -> Optional[str]:
    """Download file from manifold to temp file. Returns temp file path or None."""
    try:
        with tempfile.NamedTemporaryFile(delete=False) as temp_file:
            cmd = ["manifold", "--vip", "get", f"{corpus}/{manifold_path}"]
            _ = subprocess.run(
                cmd,
                stdout=temp_file,
                check=True,
                timeout=30,
            )
            return temp_file.name
    except Exception as e:
        if "cannot find the file" in str(e):
            print("\tSKIP: File not on manifold")
        else:
            print(f"Error downloading {manifold_path}: {e}")
        return None


def get_manifold_sha(file_path: Path) -> str:
    """Get SHA256 hash of file from manifold"""
    with open(file_path, "rb") as f:
        compressed_data = f.read()
        decompressor = zstd.ZstdDecompressor()
        decompressed_data = decompressor.decompress(compressed_data)
        return hashlib.sha256(decompressed_data).hexdigest()


def verify_files(directory: Path, corpus: str, num_files_to_check: int) -> None:
    """Verify n random files from each subdirectory against manifold."""
    non_matching_files = {}
    for subdir in directory.iterdir():
        if not subdir.is_dir():
            continue

        subdir_name = subdir.name.lower()

        files = [f for f in subdir.iterdir() if f.is_file()]
        random_files = random.sample(files, min(num_files_to_check, len(files)))

        for file_path in random_files:
            manifold_path = f"tree/{subdir_name}/{file_path.name}.zst"

            print(f"Checking: {file_path}")

            manifold_file_path = download_manifold_file(manifold_path, corpus)
            if manifold_file_path:
                try:

                    if str(file_path).endswith(".parquet.canonical"):
                        # Compare data table directly since metadata for canonical file can be different
                        decompressor = zstd.ZstdDecompressor()
                        with open(manifold_file_path, "rb") as f:
                            compressed_data = f.read()
                            decompressed_data = decompressor.decompress(compressed_data)

                            local_table = pq.read_table(file_path)
                            manifold_table = pq.read_table(BytesIO(decompressed_data))

                            if local_table.equals(manifold_table):
                                print("\tMATCH: Parquet files are identical")

                            else:
                                print("\tMISMATCH: Parquet files differ")
                                print(f"\tManifold path: {manifold_path}")
                                if subdir_name not in non_matching_files:
                                    non_matching_files[subdir_name] = []
                                non_matching_files[subdir_name].append(file_path.name)
                    else:
                        # Regular file comparison using SHA
                        local_sha = get_file_sha(file_path)
                        manifold_sha = get_manifold_sha(manifold_file_path)
                        if local_sha == manifold_sha:
                            print("\tMATCH: SHA256 hashes are identical")
                        else:
                            print("\tMISMATCH: SHA256 hashes differ")
                            print(f"\tLocal SHA: {local_sha}")
                            print(f"\tManifold SHA: {manifold_sha}")
                            print(f"\tManifold path: {manifold_path}")
                            if subdir_name not in non_matching_files:
                                non_matching_files[subdir_name] = []
                            non_matching_files[subdir_name].append(file_path.name)
                except Exception as e:
                    print(f"\tERROR: Error verifying {file_path}: {e}")

                finally:
                    os.remove(manifold_file_path)
    print("Summary:")
    for folder, files in non_matching_files.items():
        print(f"The following files in {folder} are not matching:")
        for file in files:
            print(f" - {file}")
    if not non_matching_files:
        print("All files match!")


def main(directory: str, corpus: str, num_files: int) -> None:
    """Main function to verify dataset directory against manifold corpus."""
    directory_path = Path(directory)
    verify_files(directory_path, corpus, num_files)


if __name__ == "__main__":
    import sys

    if len(sys.argv) < 3 or len(sys.argv) > 4:
        print(
            "Usage: python verify_dataset.py <directory> <manifold_corpus> [num_files]"
        )
        print(
            "  num_files: Number of random files to check per subdirectory (default: 3). Choose large number to check all (e.g. 4000)"
        )
        sys.exit(1)

    directory = sys.argv[1]
    corpus = sys.argv[2]

    num_files = 3  # default
    if len(sys.argv) == 4:
        try:
            num_files = int(sys.argv[3])
            if num_files <= 0:
                print("Error: num_files must be a positive integer")
                sys.exit(1)
        except ValueError:
            print("Error: num_files must be a valid integer")
            sys.exit(1)

    main(directory, corpus, num_files)
