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

# ML Selector Tuner

Tune XGBoost hyperparameters to optimize for compression size and speed.

## How to Use

```bash
buck2 run @//mode/opt tools/ml_selector:ml_selector_tuner -- <input_path> [population_size] [survival_rate] [max_iterations] [convergence_threshold] [mutation_rate] [compression_weight]
```

### Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `population_size` | 20 | Number of hyperparameter sets per generation |
| `survival_rate` | 0.25 | Fraction of population that survives to next generation |
| `max_iterations` | 10 | Maximum number of generations to run |
| `convergence_threshold` | 2 | Generations without improvement before stopping |
| `mutation_rate` | 0.25 | Probability of random modification to a hyperparameter |
| `compression_weight` | 0.75 | Weight for compression size vs. time (0.0–1.0) |

## How It Works

The tuner uses a genetic algorithm to search for optimal XGBoost hyperparameters:

- **Initialization**: Generates an initial population by dividing each hyperparameter range into equal intervals and randomly sampling exactly once from each interval.

- **Evaluation**: Scores each candidate by training an XGBoost model and compressing test inputs, computing a weighted score of compression size and time

- **Selection**: Ranks candidates by score and selects the top performers (based on `survival_rate`) as parents for the next generation

- **Crossover**: Creates child configurations by randomly inheriting each hyperparameter from one of two randomly selected parents

- **Mutation**: Applies random noise to genes with probability `mutation_rate`, clamping to valid bounds

- **Convergence**: Stops when max iterations are reached or the best score hasn't improved by more than 0.0005% for `convergence_threshold` consecutive generations

- **Output**: Returns the best hyperparameter configuration and compares it against default XGBoost parameters

### Tuned Hyperparameters

The following XGBoost hyperparameters are searched:

| Parameter | Range | Description |
|-----------|-------|-------------|
| `learning_rate` | 0.001–1.0 | How much model adjusts weights in response to estimated error during training |
| `min_child_weight` | 0–30 | Minimum sum of instance weight in a child |
| `subsample` | 0.1–1.0 | Fraction of samples used per tree |
| `colsample_bynode` | 0.1–1.0 | Fraction of features used per split |
| `max_depth` | 0–20 | Maximum tree depth |
| `max_leaves` | 0–60 | Maximum number of leaves per tree |
| `reg_alpha` | 0–10 | L1 regularization |
| `gamma` | 0–20 | Minimum loss reduction for split |
| `num_boost_round` | 5–60 | Number of boosting rounds |
| `max_delta_step` | 0–10 | Maximum delta step for weight estimation |
| `scale_pos_weight` | 0.5–20 | Balance of positive/negative weights (for imbalanced datasets) |
