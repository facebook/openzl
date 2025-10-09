# Dataset Manager

## Setup
Run the following command inside the `dataset_manager` directory to install required packages:
```bash
pip install -r requirements.txt
```

## Usage
Navigate to the `reproducibility` directory and use the following sample command to download all available datasets:
```bash
python -m dataset_manager.dataset_manager download -o /tmp/datasets/ -a
```

The following command lets you view all available datasets:
```bash
python -m dataset_manager.dataset_manager list
```

Use `-h` to explore all command options.
