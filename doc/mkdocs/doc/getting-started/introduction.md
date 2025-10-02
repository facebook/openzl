# Introduction

OpenZL is a framework for building lossless data compressors.
It provides a set of primitive [codecs][codecs] that can be composed in a DAG.
Additionally, it allows for user-defined control flow to modify the DAG based on the data, at any point in the compression.
OpenZL also provides a universal decompressor that can decompress anything produced by the compressor, independent of the compression DAG.


## Simple Example

``` mermaid
graph LR
    classDef hidden display: none;

    convert((Interpret as uint64));
    selector{Input is sorted?};
    intlz[FieldLZ];
    intlzdelta[FieldLZ];
    delta((Delta));
    input:::hidden --> convert;
    convert --> selector;
    selector -->|No| intlz;
    selector -->|Yes| delta;
    delta --> intlzdelta;
```

In this example, we are assuming the user input is a serialized array of 64-bit integer data stored in little-endian format.

* First, the OpenZL compressor will convert the data from bytes to native uint64 values. Note that compression will fail if the input size is not a multiple of 8 bytes.
* Next, the compressor will inspect the integer data and determine whether or not it is sorted.
    * If it is not sorted, the data will be passed to the FieldLZ compression graph, which is an LZ engine that specializes in compressing numeric data.
    * If it is sorted, the data will be passed to the delta codec, which subtracts the previous value from the current, and then the deltas will be passed to the FieldLZ compression graph.

The data compressed with this compressor can be decoded with the standard OpenZL decoder, which works on any OpenZL compressed data, no matter what compression graph was used to compress it.


## Compressing Structured Data

Although OpenZL is a toolkit of components that can be arbitrarily composed into a compression graph, most compressors end up sharing an overall structure:

``` mermaid
graph LR
  input["input"] --> Parse["Parse"]
  subgraph s1["Frontend"]
    Parse --> Group["Group"]
  end
  subgraph s2["Backend"]
    Group --> Transform["Transform"]
    Transform --> Compress["Compress"]
  end
  input:::hidden
  classDef hidden display: none
```

This is because OpenZL's components that are actually good at compressing data—its suite of transforms and compressors—work best on homogenous streams of data. For inputs that aren't already organized that way, those backend components require a frontend to parse and group the input into streams, which the backend can then compress effectively.

OpenZL offers a number of options for accomplishing that organization of your data:

1. **Pre-Built Profiles**: Use a profile that has already been built to compress a [particular format](quick-start.md#compress-with-trained-profile).

2. **Describe Your Data**: The [Simple Data Description Language](../api/c/graphs/sddl.md) profile lets you describe the format of your input with a simple syntax, which OpenZL uses to split your input into component streams.

3. **Pre-Parsed Data**: If the meat of your data can be organized as streams of homogenous content, then you can pass those streams directly into OpenZL, either as independent compressions or as a single multi-input compression.

    While the integration complexity is higher, this option may mean you can save the CPU cost of (1) serializing your data and (2) OpenZL more or less reversing that serialization in order to parse the data.

4. **Custom Parser**: Otherwise, if the above options don't work, you can [write a custom parser](examples/c/custom-formats.md) to handle your data!
