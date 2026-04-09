// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_PARTITION_ENCODE_PARTITION_BITPACK_H
#define OPENZL_CODECS_PARTITION_ENCODE_PARTITION_BITPACK_H

#include "openzl/zl_graph_api.h"

#if defined(__cplusplus)
extern "C" {
#endif

/// Dynamic graph function for partition-bitpack.
///
/// Computes optimal partition boundaries for 16-bit numeric data,
/// then routes bucket IDs to ZL_GRAPH_BITPACK and offsets to ZL_GRAPH_STORE
/// using the standard ZL_NODE_PARTITION.
///
/// Input: 1 numeric stream of 16-bit unsigned integers
/// Falls back to Store for small inputs or insufficient compression gain.
/// Falls back to Bitpack for trivial partitions.
ZL_Report EI_partitionBitpackDynGraph(
        ZL_Graph* graph,
        ZL_Edge* inputs[],
        size_t numInputs);

#if defined(__cplusplus)
}
#endif

#endif
