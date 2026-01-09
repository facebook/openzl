# ML Selector Training

Train an ML model to automatically select the best compression strategy for 64-bit integer data using the `numeric-ml-selector-64` profile.

## How to Use

### Prepare Your Training Data

Place your training files in a directory. Each file should contain 64-bit integer data.

To generate sample data:

```bash
buck2 run @//mode/opt examples/ml_selector:generate_data -- /tmp/ml_train_samples
buck2 run @//mode/opt examples/ml_selector:generate_data -- /tmp/ml_test_samples
```

Or use data from [Manifold](https://www.internalfb.com/manifold/explorer/openzl_corpus/tree/trained_numeric_datasets.tar.zstd).

### Training

Here are sample commands using generated data:

```bash
./zli train --profile numeric-ml-selector-64 /tmp/ml_train_samples -o trained_ml_sel.zli
```

Using the trained compressor, you can now compress and decompress similar data:

```bash
./zli compress -c trained_ml_sel.zli /tmp/ml_test_samples/1 -o ml_compressed.zl
```

```bash
./zli decompress ml_compressed.zl -o ml_decompressed
```

## How It Works

The ML selector uses a trained XGBoost model to predict which compression strategy will work best for a given input.

The `numeric-ml-selector-64` profile currently uses the following hardcoded successors:

- `fieldlz`
- `range_pack`
- `range_pack_zstd`
- `delta_fieldlz`
- `tokenize_delta_fieldlz`
- `zstd`

**Important**: The ordering of successors must not change between training and inference. Since the model uses numeric indices to represent successors, any change in successor ordering would cause predictions to map to incorrect compression strategies.

### Training

1. **Feature Extraction**: For each training file, the system extracts 11 statistical features:
    - `nbElts`: Number of elements in the input data
    - `eltWidth`: Size of each element in bytes (1, 2, 4, or 8)
    - `cardinality`: Estimated number of unique values
    - `cardinality_upper`: Upper bound estimate for cardinality
    - `cardinality_lower`: Lower bound estimate for cardinality
    - `range_size`: Difference between max and min values (max - min)
    - `mean`: Average value of all elements
    - `variance`: Measure of how spread out values are from the mean
    - `stddev`: Standard deviation (square root of variance)
    - `skewness`: Asymmetry of the distribution (0 = symmetric, positive = right-tailed, negative = left-tailed)
    - `kurtosis`: Higher number means dataset is more prone to outliers compared to a normal distribution


2. **Classification**: For each training sample, the system compresses it using every available successor and evaluates the results. A choice function then selects the "best" successor. By default, this is the successor that produces the smallest compressed output. The "best" successor becomes the classification for that sample.

3. **Model Training**: A XGBoost model is trained on these feature-class pairs. Once trained, the model can predict the best successor for a new file based on its extracted features. The XGBoost model is then turned into a `gbtModel`, which can be serialized.

### Inference

When you compress data with a trained model:
1. Features are extracted from the input
2. The `gbtModel` uses those features to predict which successor to use
3. Compress data with selected successor
