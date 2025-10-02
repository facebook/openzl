# Training Orchestration
The training architecture behind OpenZL makes certain assumptions about the data. The data must parse correctly according to the parser's specification, and similar categories of data should be tagged in the same way. Ideally, the samples of data will be sourced from the same place.

## Data Setup
We will extend the parsing example, and show how to use the serialized compressor for compressing and benchmarking test data in this example. To start, running `training_setup.py` provides multiple samples in our custom data format. We will use `/tmp/openzl` as the working directory for this exercise.
```
python3 training_setup.py /tmp/openzl
```
This script takes a single argument which specifies where the files are to be generated. It generates 5 samples in the `train` sub-directory and 95 samples in the `test` sub-directory.

## Running training
We can re-run our training written in the previous [exercise](custom-formats.md). This will save the compressor to the directory `/tmp/openzl/train/compressor.zlc`. Go to the directory of the cmake build and go to the `examples` directory. We can now run the training binary with the following command:
```
./training train /tmp/openzl/train /tmp/openzl/compressor.zlc
```
Using this trained compressor, we can now benchmark the test set. It is first necessary to deserialize the compressor.
```cpp
--8<-- "src/examples/training.cpp:deserialize"
```
Deserialization requires all dependencies to be registered in the compressor. In this example, we register the compressor profile we used in training with `registerGraph_ParsingCompressor`. This enables the compressor to be deserialized by calling `compressor.deserialize` and passing in the serialized compressor.

Decompression only requires that the compressor's configured format version to be less than or equal to the decompressor's format version. The following two lines of code are all that is necessary to decompress.
```cpp
--8<-- "src/examples/training.cpp:decompression"
```
Alternatively, the [CLI](../../cli.md) can be used for decompression.

## Benchmarking
We can run the binary to benchmark the performance on each file.

```
./training test /tmp/openzl/test /tmp/openzl/compressor.zlc
```
We get the result on the test set:
```
Compressed 73893166 bytes to 13233138 bytes(cMbps: 46.02 MB/s, dMbps 449.30 MB/s, 5.58x)
```
