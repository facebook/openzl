// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/graphs/small_lengths_graph.h"

#include "openzl/codecs/zl_entropy.h"
#include "openzl/codecs/zl_generic.h"
#include "openzl/codecs/zl_partition.h"
#include "openzl/codecs/zl_quantize.h"
#include "openzl/codecs/zl_sentinel.h"
#include "openzl/codecs/zl_store.h"
#include "openzl/common/assertion.h"

static const size_t kInputStoreThreshold = 50;
static const size_t kLargeStoreThreshold = 100;

ZL_Report ZL_compressSmallLengthsGraph(
        ZL_Graph* graph,
        ZL_Edge** inputs,
        size_t numInputs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);
    ZL_ASSERT_EQ(numInputs, 1);
    ZL_Edge* const edge         = inputs[0];
    const ZL_Input* const input = ZL_Edge_getData(edge);

    if (ZL_Input_numElts(input) < kInputStoreThreshold) {
        return ZL_Edge_setDestination(edge, ZL_GRAPH_STORE);
    }

    if (ZL_Input_eltWidth(input) == 1) {
        // 1-byte values go directly to Huffman
        return ZL_Edge_setDestination(edge, ZL_GRAPH_HUFFMAN);
    }

    if (!ZL_Graph_isNodeSupported(graph, ZL_NODE_SENTINEL_BYTE)) {
        // Older format versions fallback to generic compression
        return ZL_Edge_setDestination(edge, ZL_GRAPH_COMPRESS_GENERIC);
    }

    // All values < 255 go into small stream
    // Other values push 255 into small stream and go into large stream
    ZL_TRY_LET(
            ZL_EdgeList, edges, ZL_Edge_runNode(edge, ZL_NODE_SENTINEL_BYTE));
    ZL_ASSERT_EQ(edges.nbEdges, 2);

    ZL_Edge* const smallEdge = edges.edges[0];
    ZL_Edge* const largeEdge = edges.edges[1];

    // Small values go to Huffman (they are U8)
    ZL_ERR_IF_ERR(ZL_Edge_setDestination(smallEdge, ZL_GRAPH_HUFFMAN));

    const ZL_Input* const largeInput = ZL_Edge_getData(largeEdge);

    const size_t largeEltWidth = ZL_Input_eltWidth(largeInput);
    if (ZL_Input_numElts(largeInput) < kLargeStoreThreshold) {
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(largeEdge, ZL_GRAPH_STORE));
    } else if (
            largeEltWidth == 2
            && ZL_Graph_isNodeSupported(graph, ZL_NODE_PARTITION)) {
        // For 2-byte values send the lengths to partition+bitpack
        ZL_ERR_IF_ERR(
                ZL_Edge_setDestination(largeEdge, ZL_GRAPH_PARTITION_BITPACK));
    } else if (largeEltWidth == 4) {
        // TODO(T264089692): Use partition for quantize lengths once it is
        // optimized
        ZL_TRY_SET(
                ZL_EdgeList,
                edges,
                ZL_Edge_runNode(largeEdge, ZL_NODE_QUANTIZE_LENGTHS));
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(edges.edges[0], ZL_GRAPH_FSE));
        ZL_ERR_IF_ERR(ZL_Edge_setDestination(edges.edges[1], ZL_GRAPH_STORE));
    } else {
        // Don't have a good graph prepared yet, or the format version doesn't
        // support it, just use generic compression
        ZL_ERR_IF_ERR(
                ZL_Edge_setDestination(largeEdge, ZL_GRAPH_COMPRESS_GENERIC));
    }

    return ZL_returnSuccess();
}
