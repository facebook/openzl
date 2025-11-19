# ML Selector Tutorial

This tutorial would walk you through the steps needed to train and test an ML
selector in Zstrong. Currently only compiles in fbsource environment.

The example follows 4 steps:

1. Data generation
2. Extract features using training selector
3. Model training
4. Testing

## Quick start

### ML Selector

1. Generate train and test data (as alternative you can use your own data
   divided into a train and test directory):

   ```
   buck2 run @//mode/opt examples/ml_selector:generate_data -- /tmp/ml_train_samples
   buck2 run @//mode/opt examples/ml_selector:generate_data -- /tmp/ml_test_samples
   ```

   Before going forward, check current model's inference results (the starter
   model always selects FieldLz as a successor):

   ```
   buck2 run @//mode/opt examples/ml_selector:zs2_mlselector  -- infer -i /tmp/ml_test_samples/
   ```

   Example output:

   ```
   Completed compression of 3000 files with x3.76 CR (24576000 -> 6599339)
   ```

2. Run feature extraction:

   ```
   buck2 run @//mode/opt examples/ml_selector:zs2_mlselector  -- train -i /tmp/ml_train_samples/ -o /tmp/ml_features
   ```

3. Train a model and save it as the inference's new model (make sure to run this
   command from zstrong root directory):

   ```
   buck2 run @//mode/opt examples/ml_selector:train_model -- /tmp/ml_features -o examples/ml_selector/model -m EXAMPLE_MODEL
   ```

4. Test the generated model inference on the test data:

   ```
   buck2 run @//mode/opt examples/ml_selector:zs2_mlselector  -- infer -i /tmp/ml_test_samples/
   ```

   Example output:

   ```
   Completed compression of 3000 files with x3.99 CR (24576000 -> 6405903)
   ```

### Core ML Selector

1. Generate test data (as alternative you can use your own data):

   ```
   buck2 run @//mode/opt examples/ml_selector:generate_data -- /tmp/ml_train_samples
   buck2 run @//mode/opt examples/ml_selector:generate_data -- /tmp/ml_test_samples
   ```

   Before going forward, check current model's inference results (the starter
   model always selects FieldLz as a successor):

   ```
   buck2 run @//mode/lldb examples/ml_selector:zs2_core_mlselector /tmp/ml_test_samples/
   ```

   Example output:

   ```
   Completed compression of 3000 files with x3.76 CR (24579000 -> 6562994)
   ```

   You can also use the generic numeric graph to compress, the compression
   result should be better than the starter model's results.

   ```
   buck2 run @//mode/lldb examples/ml_selector:zs2_core_mlselector /tmp/ml_test_samples/ -g
   ```

   Example output:

   ```
   Using generic numeric graph
   Completed compression of 3000 files with x3.72 CR (24579000 -> 6612202)
   ```

2. Run feature extraction:

   ```
   buck2 run @//mode/opt examples/ml_selector:zs2_mlselector  -- train -i /tmp/ml_train_samples/ -o /tmp/ml_features
   ```

3. Train a model and save it as the inference's new model (make sure to run this
   command from zstrong root directory):

   ```
   buck2 run @//mode/opt examples/ml_selector:train_model -- -c /tmp/ml_features -o examples/ml_selector/core_model -m EXAMPLE_CORE_MODEL
   ```

4. Test the generated model inference on the test data:

   ```
   buck2 run @//mode/lldb examples/ml_selector:zs2_core_mlselector /tmp/ml_test_samples/
   ```

   Example output:

   ```
   Completed compression of 3000 files with x3.99 CR (24579000 -> 6508670)
   ```

You can try the same process again, with the additional flag `--enwik` to
`generate_data`, which will cause it to take parts of enwik and create samples
from them.

## Details

### Data generation

Data can be generated using the `generate_data.py` script, the script create
randomized samples for 64 integers generated as following:

1. Delta integers - creates a random series of integers with a constant
   (randomized) delta between consecutive values.
2. Tokenize delta - takes random values from a significantly smaller alphabet of
   consecutive numbers.
3. Dispatch - creates runs of random integers of similar widths, aimed at a
   graph not included in this example.
4. Enwik - will be generated only if the `--enwik` flag is given, takes the
   start of enwik9 and breaks it down into sample files.

Each enabled category would generate 1000 samples.

Note: This utility is provided just for convenience. You can use your own data
instead of generating samples with this utility, just divide your data into a
train and test sets found in two respective directories.

## Feature Generation

Accessible with the `train` subcommand to `zs2_mlselector`. The binary would
create a graph whose root is an ML training selector. It would then iterate over
all the files in the directory given in the `-i` parameter and would generate a
features file at the path specified in the `-o` parameter. It utilizes the
default integer feature generator. At the end of the run the binary would report
the best compression ratio achievable on these samples using the existing
successors to the training selector.

## Model Training

The `train_model` script takes a features files and generates a model based on
it with XGBoost. The script would then serialize the model into a format
digestible by Zstrong. The path to the output file is given with the `-o`
parameter. The `-m` parameter controls the name of the string representing the
model in the header file. For this example we use `EXAMPLE_MODEL`. At the end of
the run the script would apply the model to all of the data from the feature
file and would report on how much regression is to be expected (note the model
is trained on 70% of this data).

## Inference

Accessible with the `train` subcommand to `zs2_mlselector`. The binary would
create a graph whose root is an ML selector using the `EXAMPLE_MODEL` from the
`model.h` file. It would then iterate over all the files in the directory given
in the `-i` parameter and compress them using the model. At the end of the run
it would report the compression ratio.
