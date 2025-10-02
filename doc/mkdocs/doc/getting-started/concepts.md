# Concepts

## Data & Edges

In the OpenZL model, the nodes in the compression graphs are the codecs, and the edges are the data.

In the common case of a compression with a single input, the input data to compress is the input to the first codec in the graph.
The output of the first codec is passed to further nodes in the graph.
Finally, the outputs of the leaf codecs are passed to the [Store graph][store-graph], which is responsible for writing the data to the compressed output.

During decoding, the compressed data is read from the input, and passed to the reverse DAG, which is run to regenerate the original data.

## Codecs

A codec is a node in the compression graph that takes one or more inputs and produces one or more outputs.
Codecs run transformations on the data in order to make the data more compressible, or compress the data directly.
The codec has both an encoder and decoder, which must satisfy `decode(encode(x)) = x` for all inputs `x`.
Codecs must be present in both the compressor and decompressor.

### Standard Codecs

Standard codecs are provided by the OpenZL library.
Any graph composed of only standard codecs can always be decompressed by the standard OpenZL decoder.

### Custom Codecs

Custom codecs are provided by the user, and represent an extension to the OpenZL library.
Any graph that uses a custom codec can only be decompressed by registering the same custom codec(s) with the OpenZL decoder.

!!! WARNING
    The use of custom codecs is not recommended, because it means the compressed data is not compatible with the wider OpenZL ecosystem.

    OpenZL can be extensively customized using [graphs][graphs], which allows combining the standard codecs in unique ways, while maintaining compatibility with the standard OpenZL decompressor.


## Graphs

A compression graph, or graph for short, is a DAG that takes one or more inputs and compresses them.
The graph specifies the entire compression process for the data, where all outgoing edges are terminated by other graphs.

Users can create graphs however they please without impacting the decodability of the data.
This gives users the power to build custom compressors for their data while maintaining compatibility with the standard OpenZL decompressor.

### Standard Graph

Standard graphs are built-in graphs provided by the OpenZL library, which users can use to compress their data.
These graphs are meant to be the lowest common denominator of tools that are useful across many domains.

For example, standard graphs include:

* LZ compressors for numeric, struct, and serial data.
* Generic compression graphs that are intended to work well on any type of data.
* Entropy compression graphs like Huffman and FSE.
* Bitpacking

The typical usage pattern is for users to insert a small number of codecs or user-defined graphs that take advantage of specific properties of the data, and then pass the results to standard compression graphs.

### Static Graph

A static graph is a graph with fixed routing behavior. It accepts a single input via a head node whose output is passed to other graphs. During compression, the static graph does not make any dynamic routing decisions. The graphs that receive output from the head node, however, may still make dynamic decisions about how to compress the data.

### Selector Graph

A selector graph is a graph that takes one input, and passes it to a user-defined function that inspects the input, and determines which graph to pass the data to.
It is allowed to inspect the data during compression in order to make this decision.

For example, a selector that operates on integers may look at the input data, and determine if the data is sorted.
If the data is sorted, it may pass it to a specialized compression graph for sorted integers, otherwise it passes it to a generic integer compression graph.

### Function Graph

A function graph is a graph that takes one or more inputs, and passes it to a user defined function that defines the graph at compression time.
This function may:

* Inspect the data of any edge it has access to (including all input data).
* Run [codecs][codecs] directly on any edge, and customize the configuration of codecs that get run.
* Dynamically select the output graph for any edge.

The function graph starts with a set of unterminated edges which is the inputs. The set can change in two ways:

* Running a node removes the inputs passed to that node and adds its outputs to the set.
* Setting the output graph for an edge removes it from the set.

The set of unterminated edges must be empty before the function graph finishes.

The function graph is very powerful, but in exchange it gives up ease of use.
It is recommended to make each function graph as small as possible, and prefer setting output graphs rather than running nodes directly where possible.

### Store Graph

The store graph is the "base" graph that takes a single input and produces no outputs.
Passing data to this graph denotes that the data should be written as-is into the compressed output.
All other graphs must eventually terminate all of their edges with store.
However, graphs don't always terminate explicitly with the store graph. For example, a graph might terminate their edges with a standard graph, which eventually routes all edges to the store graph.
