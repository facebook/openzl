# Split By Range

## Overview

`split_byrange` is a numeric codec that automatically detects and splits a series
of numeric values into contiguous segments when the values belong to different
numeric ranges. This is useful when a single column effectively concatenates
data from multiple sources or measurement setups, each producing values in a
distinct range.

The operation is built on top of the existing `splitN` codec: it shares the same
wire format and decompression path. Only the compression side adds a new
range-detection policy that automatically computes segment boundaries.

## Algorithm: Block-Based Range Boundary Detection

The detection algorithm uses a single-pass block-level scan with confirmation
windows to find boundaries between contiguous segments whose values live in
non-overlapping ranges.

### Definitions

Given a numeric input sequence `v[0..N-1]`, a **range segment** is a maximal
contiguous sub-sequence `v[start..end-1]` such that all values fall within a
single range `[lo, hi]`, and this range does not overlap with the ranges of
adjacent segments.

### Algorithm Steps

#### Phase 1: Block Statistics

Divide the input into fixed-size blocks and compute the min and max value
of each block. The block size is derived from the minimum segment size
(typically 4 elements per block).

#### Phase 2: Block-Level Split Confirmation

Scan block boundaries left to right. At each candidate boundary, examine
a **confirmation window** of M blocks on each side (default M=3):

- Compute the min/max of the left window and the right window.
- Check that the two windows have **non-overlapping ranges**.
- Check **stability**: the range spanned by each window must not be much
  larger than the largest individual block range within it. This rejects
  monotonic progressions (e.g., steadily increasing values) where adjacent
  blocks happen to be non-overlapping but belong to the same logical range.

A confirmed split advances the scan past the boundary so that subsequent
left windows do not include blocks from the previous segment.

#### Phase 3: Element-Level Refinement

For each confirmed block-level split, binary search within the block to
find the exact element position where the boundary occurs, using
element-level prefix/suffix min-max comparison.

### Properties

- **O(N) time**: A single forward pass over block statistics, plus
  O(blockSize) refinement per confirmed split.
- **O(N / blockSize) space**: One min and one max value per block.
- **Deterministic**: Same input always produces the same split.
- **Minimum segment size**: Controlled by an optional parameter (default: 16).
  A split is rejected if either side would have fewer elements than this
  threshold. Inputs with fewer than `2 * minSegmentSize` total elements
  are passed through as a single segment.

### Capabilities

The algorithm excels at detecting boundaries between contiguous blocks of
values from non-overlapping ranges, including:

- **Monotonic sequences**: Ascending or descending range transitions
  (e.g., `[0,100]` then `[500,600]` then `[1000,1100]`).
- **Valley/peak patterns**: High-low-high or low-high-low sequences
  (e.g., `[1000,1100]` then `[0,50]` then `[2000,2100]`).
- **Repeated ranges**: The same range value interval appearing at multiple
  non-adjacent positions (e.g., `A, B, A` where A and B are non-overlapping)
  is correctly split at each boundary.

For contiguous non-overlapping ranges, the algorithm is guaranteed to find
**all** boundaries (subject to the minimum segment size constraint).

### Unsigned Interpretation

The current implementation interprets all input values as **unsigned** integers.
This means negative values (when the source data is signed) are treated as
their unsigned bit-pattern equivalents, which may prevent the algorithm from
detecting meaningful range boundaries in signed data.

Support for signed interpretation is planned for a future version.

### Limitation: Minimum Segment Size

Each segment must contain at least `minSegmentSize` elements (default 16)
for the algorithm to detect the boundary. Patterns where ranges alternate
at a granularity smaller than this threshold — such as element-level
interleaving `[low, high, low, high, ...]` or very short bursts — will
not be split.


## Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `minSegmentSize` | int | 16 | Minimum number of elements per segment. A split is rejected if either side would have fewer elements than this value. |

The node works without any parameters — the default `minSegmentSize` of 16
provides a good balance between detecting short segments and avoiding false
positives from noise.

## Usage

### C API

```c
#include <openzl/codecs/zl_split.h>

// Default (no parameters needed):
ZL_GraphID graph = compressor.buildStaticGraph(
    ZL_NODE_SPLIT_BYRANGE, successorGraph);
```

### C++ API

```cpp
#include <openzl/cpp/codecs/Split.hpp>

// Default: auto-detects range boundaries with minSegmentSize=16
auto graph = compressor.buildStaticGraph(
    nodes::SplitByRange{}, {graphs::Compress{}()});

// Custom: allow smaller segments (minimum 4 elements per segment)
auto graph = compressor.buildStaticGraph(
    nodes::SplitByRange{4}, {graphs::Compress{}()});
```

### Python API

```python
import openzl

compressor = openzl.Compressor()
graph = compressor.build_graph(
    openzl.nodes.SplitByRange(), openzl.graphs.Compress())
```
