# Dataset Manager

## Setup
For `era5` datasets, make sure GDAL is installed using the instructions [here](https://mothergeo-py.readthedocs.io/en/latest/development/how-to/gdal-ubuntu-pkg.html#install-gdal-for-python). If you don't need these datasets, you can skip this step.

For `.parquet` files (`binance` datasets), run the following commands to compile binary files:

```bash
mkdir -p cmakebuild
cmake -S . -B cmakebuild -DOPENZL_BUILD_PARQUET_TOOLS=ON
cmake --build cmakebuild
```
If you choose to compile binary files in separate location, please pass in the path to the `make_canonical_parquet` binary with the `--binary-path` command

Run the following command inside the `dataset_manager` directory to install required packages:
```bash
pip install -r requirements.txt
```

Some datasets require API keys for access. Set up the following keys as needed:

**For Kaggle datasets:**
- Follow the instructions under `Authentication`: https://www.kaggle.com/docs/api

**For Climate Data Store datasets:**
- Follow the instructions under `Setup the CDS API personal token`: https://cds.climate.copernicus.eu/how-to-api


## Usage
Navigate to the `reproducibility` directory and use the following sample command to download all available datasets:
```bash
python -m dataset_manager.dataset_manager download -o /tmp/datasets/ -a
```

A specific dataset can be downloaded with `-d` and parquet binary can be passed in with `--binary-path`. Below is a sample command:
```bash
python -m dataset_manager.dataset_manager download -o /tmp/datasets -d binance --binary-path=$HOME/openzl/cmakebuild/tools/parquet/make_canonical_parquet
```

The following command lets you view all available datasets:
```bash
python -m dataset_manager.dataset_manager list
```

Use `-h` to explore all command options.
