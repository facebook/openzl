---
name: unitbench-openzl-scenarios
description: Use when creating benchmark scenarios for new openzl codec nodes in unitBench - adding kernel-level encode/decode benchmarks or graph-level compress/decompress benchmarks for codecs like bitsplit, delta, transpose, entropy, etc.
---

# unitBench Scenario Creation

Create benchmark scenarios for openzl codec nodes. Two benchmark types exist: **kernel** (test encode/decode kernel functions directly) and **graph** (test a node within a full compress/decompress pipeline).

## Deciding What to Benchmark

Before creating scenarios, ask the user:

1. **Does this node have a standalone kernel function?** (e.g., `ZL_bitSplitEncode`, `ZL_bitSplitDecode`)
   - If yes: kernel benchmarks are an option - test the encode/decode functions directly with minimal overhead.
   - If no: the node must be tested as part of a graph.

2. **Should the node be tested in a graph?**
   - Graph benchmarks test the full pipeline: tokenization -> node -> downstream processing -> round-trip decompression.
   - Useful for measuring real-world overhead vs kernel-only performance.

3. **What data types/widths does the node operate on?**
   - Determines element widths, bit layouts, and which tokenizer node to use in graphs.
   - Ask the user for the specific parameters (bitWidths, element widths, etc.).

## File Locations

| What | Where |
|------|-------|
| Kernel benchmarks | `benchmark/unitBench/scenarios/codecs/<name>.c` and `.h` |
| Graph benchmarks | `benchmark/unitBench/scenarios/<name>_graph.c` and `.h` |
| Scenario registration | `benchmark/unitBench/benchList.h` |
| BUCK file | `benchmark/unitBench/BUCK` |
| Test data | `/tmp/` (use `dd if=/dev/urandom`) |

All paths relative to `fbcode/openzl/dev/`.

## Kernel Benchmark

Test encode/decode kernel functions directly. Requires a standalone kernel API.

### Header (`.h`)

Add declarations to existing `scenarios/codecs/<codec>.h` or create a new one:

```c
// Decode
size_t <codec>Decode_<type>_prep(void* src, size_t srcSize, const BenchPayload* bp);
size_t <codec>Decode_<type>_outSize(const void* src, size_t srcSize);
size_t <codec>Decode_<type>_wrapper(const void* src, size_t srcSize, void* dst, size_t dstCapacity, void* customPayload);

// Encode
size_t <codec>Encode_<type>_prep(void* src, size_t srcSize, const BenchPayload* bp);
size_t <codec>Encode_<type>_outSize(const void* src, size_t srcSize);
size_t <codec>Encode_<type>_wrapper(const void* src, size_t srcSize, void* dst, size_t dstCapacity, void* customPayload);
```

### Source (`.c`)

**Decode scenario:** prep packs split streams contiguously into src, wrapper recomputes pointers and calls the decode kernel, outSize returns `(srcSize / sumSrcElt) * dstEltWidth`.

**Encode scenario:** prep fills src with random values, wrapper calls the encode kernel writing streams contiguously into dst, outSize returns `(srcSize / srcEltWidth) * sumDstElt`.

**Reference implementation:** See `scenarios/codecs/bitSplit.c` for the complete pattern with multiple data type examples.

## Graph Benchmark

Test a node within a full compress/decompress graph. Required when no standalone kernel exists. Also useful alongside kernel benchmarks to measure graph overhead.

### Header (`.h`)

```c
// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef GUARD_MACRO_H
#define GUARD_MACRO_H

#include "openzl/shared/portability.h"
#include "openzl/zl_compressor.h"

ZL_BEGIN_C_DECLS

ZL_GraphID <name>_graph(ZL_Compressor* cgraph);

ZL_END_C_DECLS

#endif
```

### Source (`.c`)

Build the graph using `ZL_Compressor_registerStaticGraph_fromNode1o`. Typical pattern: tokenize input -> apply node -> downstream graph.

```c
#include "openzl/codecs/zl_<codec>.h"   // ZL_NODE_<YOUR_NODE>
#include "openzl/zl_compressor.h"
#include "openzl/zl_public_nodes.h"      // ZL_NODE_INTERPRET_AS_LE*

ZL_GraphID my_graph(ZL_Compressor* cgraph)
{
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        abort();
    }
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_compressionLevel, 1))) {
        abort();
    }

    return ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_AS_LE64,  // tokenizer matching element width
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    cgraph, ZL_NODE_YOUR_NODE, ZL_GRAPH_STORE));
                    // ZL_GRAPH_STORE to benchmark node in isolation
                    // ZL_GRAPH_ZSTD to benchmark with compression
}
```

**Tokenizer node must match element width:** `ZL_NODE_INTERPRET_AS_LE16` (2 bytes), `ZL_NODE_INTERPRET_AS_LE32` (4 bytes), `ZL_NODE_INTERPRET_AS_LE64` (8 bytes).

**Reference:** See `scenarios/sao_graph.c` for a complex multi-stream graph example.

## Registration

### benchList.h

1. Add include for graph header (near other graph includes around line 171):
```c
#include "benchmark/unitBench/scenarios/<name>_graph.h"
```

2. Add entries to `scenarioList[]` array (**maintain alphabetical order**):
```c
// Kernel scenarios (set .func via first positional arg)
{ "<codec>Decode_<type>", <codec>Decode_<type>_wrapper, .prep = <codec>Decode_<type>_prep, .outSize = <codec>Decode_<type>_outSize },
{ "<codec>Encode_<type>", <codec>Encode_<type>_wrapper, .prep = <codec>Encode_<type>_prep, .outSize = <codec>Encode_<type>_outSize },

// Graph scenario (set .graphF - harness auto-wires init and compression)
{ "<graphName>", .graphF = <name>_graph },
```

### BUCK

Add a library target for graph benchmarks (following `sao_graph` pattern):
```python
zs_library(
    name = "<name>_graph",
    srcs = ["scenarios/<name>_graph.c"],
    headers = ["scenarios/<name>_graph.h"],
    deps = [
        "../..:zstronglib",
    ],
)
```

Kernel `.c`/`.h` files are auto-included by the unitBench binary's `glob(["**/*.c"])`.

## Test Data and Running

**Test data size must be a multiple of the element width** for the codec/node being tested. For example, fp64 (8-byte elements) needs a file size divisible by 8. Using standard sizes like 1MB/10MB works for all common element widths.

```bash
# Generate test data (use sizes that are multiples of element width)
dd if=/dev/urandom of=/tmp/openzl_bench/test_1MB.bin bs=1M count=1
dd if=/dev/urandom of=/tmp/openzl_bench/test_10MB.bin bs=1M count=10

# Build with make (optimized -O3, no ASAN - use this for benchmarking) (located in fbcode/openzl/dev/)
make unitBench

# Run benchmark
./unitBench <scenarioName> /tmp/openzl_bench/test_10MB.bin

# Useful options
#   -i <seconds>    benchmark duration (default ~2s)
#   -B <bytes>      split input into blocks
#   --csv           CSV output for parsing
#   -z              compression only (skip decompression round-trip)

# List all scenarios
./unitBench --list
```

**Do NOT use buck for benchmarking** - buck builds include ASAN and debug flags that make results 10-50x slower than production. Use `make unitBench` for representative numbers.

## Common Mistakes

- **Forgetting prep function:** Kernel decode benchmarks need prep to fill split streams. Encode benchmarks need prep to fill random source values.
- **Wrong outSize:** Decode: `(srcSize / sumSrcElt) * dstEltWidth`. Encode: `(srcSize / srcEltWidth) * sumDstElt`.
- **Graph not setting formatVersion:** Must set `ZL_CParam_formatVersion` to `ZL_MAX_FORMAT_VERSION` for newer nodes.
- **scenarioList not alphabetical:** Entries must be in alphabetical order by name.
- **Test data in repo:** Put test data in `/tmp/`, not in the source tree.
