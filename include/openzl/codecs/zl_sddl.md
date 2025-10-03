## Introduction

OpenZL's backend compression graphs are most effective on streams of self-similar, typed data.

The Simple Data Description Language (SDDL) module provides a lightweight tool to decompose simple structured data formats into their components, so that they can be effectively compressed by OpenZL's backends.

The SDDL functionality is comprised of a few pieces:

* The SDDL graph, which takes a compiled description and uses it to parse and split up the input into a number of output streams.

* The SDDL compiler, which takes a description written in the SDDL language and translates it into the binary format that the SDDL graph accepts.

* The SDDL profile, a pre-built compressor available in the CLI which runs SDDL on the input and passes all of its outputs to the generic clustering graph.

These components and how to use them are described below.

--8<-- "src/tools/sddl/compiler/Syntax.md"

## Running SDDL

### The SDDL Profile

The easiest way to run SDDL over an input is via the SDDL profile built into the CLI.

!!! example
    Start by writing an SDDL Description for your data. Here's a trivial one that splits the input into alternating integer streams:

    ```sh
    cat <<EOF >desc.sddl
    Row = {
      UInt32LE
      UInt32LE
    }
    num_rows = _rem / sizeof Row
    : Row[num_rows]
    : Byte[_rem]
    EOF
    ```

    Then compress an input using that description:

    ```sh
    ./zli compress --profile sddl --profile-arg desc.sddl --train-inline my_input_file -o my_input_file.zl
    ```

    Since the SDDL profile passes the results of the parse produced by the SDDL graph to the generic clustering graph, which needs to be trained, the `--train-inline` flag is important to get good performance.

    If you are compressing many inputs with the same profile, it's much faster to do the training once and use the resulting trained profile for each input rather than training on each and every input separately:

    ```sh
    ./zli train --profile sddl --profile-arg desc.sddl input_dir/ -o trained_sddl.zlc

    for f in $(ls input_dir/); do
      ./zli compress --compressor trained_sddl.zlc input_dir/$f -o output_dir/$f.zl
    done
    ```

### The SDDL Graph

The SDDL Graph allows you to integrate SDDL into compressors other than the prebuilt SDDL profile. You can create an SDDL graph with `ZL_Compressor_buildSDDLGraph`:

::: ZL_Compressor_buildSDDLGraph

The SDDL Graph has the following structure:

``` mermaid
flowchart TD
    subgraph SDDL Graph
        Desc([Description]);
        Input([Input]);
        Conv@{ shape: procs, label: "Type Conversions"};
        Engine[SDDL Engine];
        Inst([Instructions]);
        Disp[/Dispatch Transform\];
        Succ[Successor Graph];

        Desc --> Engine;
        Input --> Engine;
        Engine --> Inst;
        Inst -->|Dispatch Instructions| Disp;
        Input --> Disp;
        Inst -->|Type Information| Conv;
        Disp ==>|Many Streams| Conv;
        Conv ==>|Many Streams| Succ;
    end

    OuterInput[ZL_Input] --> Input;
    OuterParam[ZL_LocalCopyParam] --> Desc;
```

This graph takes a single serial input and applies the given description to it, using that description to decompose the input into fields which are mapped to one or more output streams. These streams, as well as two control streams are all sent to a single invocation of the successor graph. The successor must therefore be a multi-input graph able to accept any number of numeric and serial streams (at least).

(The control streams are: a numeric stream containing the stream indices into which each field has been placed and a numeric stream containing the size of each field. See also the documentation for `dispatchN_byTag` and particularly, `ZL_Edge_runDispatchNode()`, which is the underlying component that this graph uses to actually decompose the input, for more information about the dispatch operation. These streams respectively are the first and second stream passed into the successor graph, and the streams into which the input has been dispatched follow, in order.)

The streams on which the successor graph is invoked are also tagged with int metadata, with key 0 set to their index. (For the moment. Future work may allow for more robust/stable tagging.) This makes this graph compatible with the generic clustering graph (see `ZL_Clustering_registerGraph()`), and the `sddl` profile in the demo CLI, for example, is set up that way, with the SDDL graph succeeded by the generic clusterer.

The description given to the graph must be pre-compiled. Use the SDDL Compiler to translate your description to the compiled representation that the graph accepts:

### The SDDL Compiler

#### API

::: openzl::sddl::Compiler::compile

#### Compiled Representation

The SDDL engine accepts, and the SDDL compiler produces, a translated / compiled representation of the description, which is a CBOR-serialized expression tree.

!!! danger
    The compiled format is unstable and subject to change!

    You should not expect SDDL descriptions compiled with one version of OpenZL to work with SDDL graphs from other versions of OpenZL. Nor should you currently build codegen that targets this unstable format.
