// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_MERGE_SORTED_H
#define ZSTRONG_CODECS_MERGE_SORTED_H

#include "openzl/zl_nodes.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Merges and deduplicates 0-64 strictly increasing runs of u32s.
//
// WARNING: This node will fail compression if there are more than 64 sorted
// runs!
//
// Input: u32: The input is broken up into runs of strictly increasing u32s.
// There must be <= 64 of these runs. These runs are merged in strictly
// increasing order into Output 1. For every value in the merged list, there is
// a corresponding bitset in Output 0. The b'th bit of the bitset is set to 1
// iff the b'th run contains the corresponding merged value. Output 0: Numeric:
// Bitset with one bit per sorted run. The width is determined
//           by the number of sorted runs.
// Output 1: Numeric: The merged list of strictly increasing u32s
#define ZL_NODE_MERGE_SORTED ZL_MAKE_NODE_ID(ZL_StandardNodeID_merge_sorted)

/**
 * Creates a graph for ZL_NODE_MERGE_SORTED that first detects whether
 * the input has <= 64 sorted runs. If it does it selects the node.
 * Otherwise it selects the backupGraph.
 */
ZL_GraphID ZL_Compressor_registerMergeSortedGraph(
        ZL_Compressor* cgraph,
        ZL_GraphID bitsetGraph,
        ZL_GraphID mergedGraph,
        ZL_GraphID backupGraph);

#if defined(__cplusplus)
}
#endif

#endif
