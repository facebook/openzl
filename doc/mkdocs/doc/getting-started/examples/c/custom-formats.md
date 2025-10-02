# Compressing Custom Data Formats
A simple way to enhance generic compressors is to parse the input data before compression. Furthermore, if the data is homogeneous training can also be done. After parsing into structured outputs, OpenZL offers a training tool that clusters these outputs to exploit correlations and tries multiple compression strategies per cluster to create a good compressor.

## Running the example
First generate some data using `parsing_data_generator_correlated.py` (which can be found in `openzl/examples` folder). This is in its own [custom format](#the-data-format). We will use `/tmp/openzl` as the working directory for this exercise and generate a single file to compress. [Training Orchestration](training-orchestration.md) will properly set up a test/train split for your data. For now, we will train and test on the single file. Inside `/tmp/openzl`, create a `train` directory and an empty file called `correlated`. The following command will populate the file with data:

Run:
```
python3 parsing_data_generator_correlated.py /tmp/openzl/train/correlated
```
Next, run cmake. If you used [these cmake steps](../../quick-start.md#building-the-openzl-cli) to build `zli` you can run the following command to run cmake.
```
cmake --build cmakebuild --target training
```
Then go to the `cmakebuild/examples` directory and run the training binary:
```
./training train /tmp/openzl/train /tmp/openzl/compressor.zlc
```
Now if you run:
```
./training test /tmp/openzl/train /tmp/openzl/compressor.zlc
```
You should get the following output (note training is non-determinisitc so results may vary):
```
Compressed 854156 bytes to 114502 bytes(cMbps: 191.95 MB/s, dMbps 553.57 MB/s, 7.46x)
```
We can compare this to zstd lv1 to lv10 where we get:
```
 1#correlated        :    854156 ->    478813 (x1.784),  288.0 MB/s,  727.3 MB/s
 2#correlated        :    854156 ->    354016 (x2.413),  253.4 MB/s,  789.9 MB/s
 3#correlated        :    854156 ->    344204 (x2.482),  168.0 MB/s,  780.2 MB/s
 4#correlated        :    854156 ->    337962 (x2.527),  134.1 MB/s,  774.5 MB/s
 5#correlated        :    854156 ->    327736 (x2.606),   86.8 MB/s,  743.6 MB/s
 6#correlated        :    854156 ->    327251 (x2.610),   69.5 MB/s,  795.9 MB/s
 7#correlated        :    854156 ->    325568 (x2.624),   67.1 MB/s,  787.6 MB/s
 8#correlated        :    854156 ->    324584 (x2.632),   57.9 MB/s,  785.7 MB/s
 9#correlated        :    854156 ->    324416 (x2.633),   54.4 MB/s   786.7 MB/s
10#correlated        :    854156 ->    324350 (x2.633),   50.3 MB/s,  795.1 MB/s
```
This page will explain the process of writing a parser for a custom data format that can be used with training to achieve these results.

## Training
The following diagram illustrates the flow of the data through the system to produce a trained compressor.
``` mermaid
graph LR
    classDef hidden display: none;

    parser[Parser];
    trainer[Trainer];
    serializer[Compressor Serializer];

    input1:::hidden  -->|Training Data| parser;
    parser --> |Parsed output|trainer;
    parser --> |Parsed output|trainer;
    parser --> |Parsed output|trainer;
    trainer --> |ClusteringConfig| serializer;
    serializer --> |Serialized Compressor|output:::hidden
```

In this three step process of parsing, clustering and compressing, the trainer is designed to handle the choice of how to cluster and compress. The parser produces structured outputs, then the trainer searches the space of compressors with the parsed outputs grouped (or clustered) in different ways, and uses the provided set of successors to produce a good compressor.

``` mermaid
graph LR
    classDef hidden display: none;
    parser[Parser];
    cluster[Clustering Graph];
    Succ1[Successor 1];
    Succ2[Successor 2];
    input:::hidden ---> |Serialized Input|parser
    parser ---> |Parsed Output|cluster
    parser ---> |Parsed Output|cluster
    parser ---> |Parsed Output|cluster
    parser ---> |Parsed Output|cluster
    cluster ---> |Clustered outputs|Succ1
    cluster ---> |Clustered outputs|Succ1
    cluster ---> |Clustered outputs|Succ2
```
!!! Note
    The clustering graph is a standard graph in OpenZL that is ingests configuration to perform clustering. This allows a configuration drives the graph to choose how parsed inputs are clustered, and the successors these clustered inputs go to. The trainer works by paramaterizing the clustering graph with different configurations, and searching for the parametrization that compresses optimally. The `ZL_Clustering_Config` struct is the configuration used for the clustering graph.

Since the search space is very big and running compression is required to test the performance of a compressor, this operation is quite slow if ran on the entire set of data which motivates separating the data into training and test sets. As long as the training set has similar compressability properties to the test set, it is sufficient to search on a small training set to find correlation and the correct set of successors for the output to use.

This page presents a toy example using a custom data format on how to write a parser, and integrate it with training. The full example source is [here](#full-example-source). We will highlight important snippets of the code to explain the process.

## Parsing

### The data format
This section describes the "numeric arrays" format assumed by the example. Conceptually, this is a concatenation of an unspecified number of 1, 2, 4, or 8-byte wide integers.
The file consists of some number of *array sectors*, each with the following format:

1. First, a 4-byte little-endian integer `n` representing the number of bytes in the data section of this array.
2. A 1-byte number `w` specifying the width of the data in bytes.
3. A 4-byte little-endian integer `t` representing the numeric tag of the following data block. This tag is an identifier for the data source of the block where using the same tag indicates that the data is similar in structure with other blocks with the same tag. We will explain in more detail [later](#setting-output-metadata) why this field is useful.
4. A data block containing `n` bytes, representing an array of `n / w` integers of width `w`.

In this exercise, we use the generator script `examples/parsing_data_generator_correlated.py` to generate random data that fits this data format. We generate a single file to do basic testing.

### Writing a parser
The first step of this process is to write a parser for this data format. Parsing is a component of the compressor and a function graph is the way to implement this component in OpenZL.

The general strategy is:

1. Create a [ZL_FunctionGraphFn][] that takes the unparsed input
2. Lex the input into separate outputs based on semantic meaning
3. `ZL_Edge_runDispatchNode()` to separate the input into constituent outputs based on the lexed chunks.
4. Send each constituent output to a successor graph.

### Setting up the parser
OpenZL dictates that every graph function matches the signature of [ZL_FunctionGraphFn][].

Here, the unparsed input will live in the first input, so we grab pointers to the raw data.
```cpp
--8<-- "src/examples/training.cpp:get-data"
```
The end goal is to generate a bunch of parsed outputs using `dispatch`. But before we can do that, we must lex the input so the dispatcher knows what to do. The dispatch is called with the following snippet:
```cpp
--8<-- "src/examples/training.cpp:dispatch"
```
!!! Note
    Under the hood, `ZL_Edge_runDispatchNode()` calls `ZL_NODE_DISPATCH`, which takes input a list of sizes `segmentSizes[]` and dispatch indices `tags[]`. It will iteratively copy chunks of size `segmentSizes[i]` to output `tags[i]` until the entire segment list has been processed.

Our next goal is to lex the input to populate these segment sizes and tags so the dispatch node splits the input properly.

### Lexing the input
For this example, we aim to lex the input such that the same types of data are grouped. We define a tag for number of bytes, element width and numeric tags. We also need to track the tags of the data blocks.

We need to track the mapping from tags to dispatch indices, so we create a mapping for this, and have a counter `currentDispatchIdx` to track the total number of unique tags that each correspond to an output. This will allow us to ensure all inputs with the same tag are dispatched to the same output. It is also necessary to store a reverse mapping to pass the necessary metadata to the clustering graph later. This will be explained in more detail [later](#setting-output-metadata).
```cpp
--8<-- "src/examples/training.cpp:tag-defs"
```
The lexing is a fairly straightforward exercise of going through the serial input and populating the segments and taglist as well as numeric tags and element widths. We record the dispatch indices according to the map we created.
```cpp
--8<-- "src/examples/training.cpp:lex"
```

### Setting output metadata
We can set metadata on the output to provide information to future graphs or nodes that will use that output. The clustering graph (implemented in `src/openzl/compress/graphs/generic_clustering_graph.c`) is a standard graph in OpenZL that uses metadata of its inputs to figure out how it should be clustered. It is compulsory to tag each output if training is going to be run on the parsed outputs.

The purpose of tags is for the trainer to be able to identify homogeneous inputs. The trainer builds a compressor with configuration that always compresses data with the same tag and type with the same successor. For example, if a dataset contains a column of names of cities, and is split across multiple files, it is the parser's responsibility to tag the column of names with the same tag for every file. This way, in a future unseen file, the column of city names can be compressed in the same manner. It is ideal to give separate tags to different 'columns' of the same file, because training can make decisions about clustering at the tag granularity.

In our case, we have five fixed inputs with different types of information. We give each of these outputs a different metadata tag and store all output `ZL_Edge*` pointers in a unified `std::vector` for convenience. It is better to give a different metadata tag if it is unknown if outputs are similar since the trainer has the ability to group the data if it is better grouped.
```cpp
--8<-- "src/examples/training.cpp:metadata"
```

We then handle remaining numeric array outputs and set metadata for them too.

### Type conversions
The dispatch node interprets outputs data as a `ZL_Type_Serial` type, therefore to take advantage of the fact the data is inherently LE data of a specified fixed width, we need to run a conversion node on the output. `ZL_Node_interpretAsLE` interprets serial input data as LE data with the specified number of bits, returning output data with `ZL_Type_Numeric` type.
```cpp
--8<-- "src/examples/training.cpp:conversion"
```
We set metadata according to what was specified by the custom data format and append to a unified `std::vector` for convenience.

### Sending outputs to a successor
We set up a custom graph as the destination for all the parsed outputs. We check that a custom graph has been provided and send the outputs all to this graph.
```cpp
--8<-- "src/examples/training.cpp:custom-graphs"
```
We can use `ZL_GRAPH_COMPRESS_GENERIC` for this graph if we do not want to use training and just want to compress our output. This gives the following result:
```
Compressed 854156 bytes to 179900 bytes
```
Currently, `ZL_GRAPH_COMPRESS_GENERIC` does not utilize any correlation across outputs and uses a fixed successor for all outputs of a given type. The generic clustering graph is the intended destination in this scenario.

### Graph registration
A graph used in a compressor must be registered on the compressor first if it is a non-standard graph.
```cpp
--8<-- "src/examples/training.cpp:register-parser-base"
```
In the above example, it is necessary to register the base component of the graph first, before parameterizing it for compressor serializability. We want the compressor to be serializable in this scenario as we expect to do training, where in applications it is natural to save the compressor to be used later.

For the graph to be serializable, its base graph must be registered without `LocalParams` and without `customGraphs`. We use the `registerFunctionGraph` function to register this graph.

The next step is to paramaterize the graph with the required parameters. In our case, the graph requires the parameterized clustering graph to be passed in. In the previous scenario where we stated `ZL_GRAPH_COMPRESS_GENERIC` was used to compress the input, we pass in the graph registered via `ZL_Clustering_registerGraph` as the clustering graph instead. We use the function `parameterizeGraph` on the `Compressor` object to do this parameterization.
```cpp
--8<-- "src/examples/training.cpp:register-parser-parameterize"
```

After creating a generic registration function for the parser, we must create a specialized compressor profile for the parser. The compressor profile allows you to specify successors, and optionally clustering codecs. Good successors to pick here depends on the parsed data. For example, time series data will work well with compressors which have `ZL_NODE_DELTA`. A reasonable set of successors can be found in `custom_parsers/shared_components/clustering.cpp`. We have also built a [graph builder]()(Documentation under construction) that can be used at this stage of successor selection.

## Running training and compression
 After handling I/O, create a compressor, register the new compressor profile built and set the starting graph for the compressor as this graph. Then we can can train directly by calling `train` with the appropriate parameters.
```cpp
--8<-- "src/examples/training.cpp:train"
```
One of the required parameters `compressorGenFunc` requires a function to create a compressor from a serialized compressor registering dependencies.
```cpp
--8<-- "src/examples/training.cpp:compressor-creation"
```
The `train` function returns a serialized compressor which can be saved. In this example, we ignore the serialized compressor and directly test the trained compressor on our data.
```cpp
--8<-- "src/examples/training.cpp:compress"
```

## Training Orchestration
Read [here](training-orchestration.md) for how to set up some basic training orchestration. So far, we have tested on a single file. This will provide more details on how to use compressor serialization and set up a test/train split for your data.

## Full example source
```cpp title="examples/training.cpp"
--8<-- "src/examples/training.cpp"
```
