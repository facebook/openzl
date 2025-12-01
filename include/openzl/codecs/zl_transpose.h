// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_CODECS_TRANSPOSE_H
#define ZSTRONG_CODECS_TRANSPOSE_H

#include <stddef.h>

#include "openzl/zl_graph_api.h"
#include "openzl/zl_nodes.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Transpose Split
// Input : 1 fixed-size-field stream
// Output : eltWidth serialized streams of size nbElts
// Result : convert a stream of N fields of size S
//          into S streams of N fields
// Example : 1 2 3 4 5 6 7 8 as 2 fields of size 4
//           => transposed into 4 streams as 2 fields of size 1
//           => (1, 5), (2, 6), (3, 7), (4, 8)
#define ZL_NODE_TRANSPOSE_SPLIT \
    ZL_MAKE_NODE_ID(ZL_StandardNodeID_transpose_split)

/**
 * Helper function to create a graph for ZL_NODE_TRANSPOSE_SPLIT.
 *
 * For frame format versions >= 11 ZL_NODE_TRANSPOSE_SPLIT is used and
 * any eltWidth is supported.
 * For frame format versions < 11 only eltWidth = 1, 2, 4, 8 is supported.
 * Using other sizes will fail compression.
 *
 * Input: A fixed-size-field stream
 * Output: eltWidth serialized streams of size nbElts
 * Result: Convert a stream of N fields of size S into S streams of N fields by
 * transposing the input stream.
 * Example : 1 2 3 4 5 6 7 8 as 2 fields of size 4
 *           => transposed into 4 streams as 2 fields of size 1
 *           => (1, 5), (2, 6), (3, 7), (4, 8)
 */
ZL_GraphID ZL_Compressor_registerTransposeSplitGraph(
        ZL_Compressor* cgraph,
        ZL_GraphID successor);

/**
 * @returns a NodeID that implements transpose split for the given @p eltWidth
 * that will work with any Zstrong format version. If no node exists, then
 * returns ZL_NODE_ILLEGAL. This can happen for format version <= 10 when
 * @p eltWidth != 2,4,8.
 */
ZL_NodeID ZL_Graph_getTransposeSplitNode(const ZL_Graph* gctx, size_t eltWidth);

ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runTransposeSplit(ZL_Edge* edge, const ZL_Graph* graph);

#if defined(__cplusplus)
}
#endif

#endif
