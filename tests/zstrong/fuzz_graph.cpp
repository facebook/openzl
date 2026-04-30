// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/encoder_registry.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/compress/graph_registry.h"
#include "openzl/compress/implicit_conversion.h"
#include "openzl/zl_common_types.h" // ZL_TernaryParam_enable
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_reflection.h"
#include "tests/constants.h"
#include "tests/datagen/DataGen.h"
#include "tests/fuzz_utils.h"

#include <algorithm>

namespace openzl {
namespace tests {
namespace {

std::vector<ZL_NodeID> getAllNodes(uint32_t formatVersion)
{
    std::vector<ZL_NodeID> nodes(ER_getNbStandardNodes());
    ER_getAllStandardNodeIDs(nodes.data(), nodes.size());

    auto cgraph = ZL_Compressor_create();
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, formatVersion));
    auto end = std::remove_if(
            nodes.begin(), nodes.end(), [cgraph](ZL_NodeID node) {
                if (ZL_Compressor_Node_getNumInputs(cgraph, node) != 1) {
                    return true;
                }
                size_t const nbSuccessors =
                        ZL_Compressor_Node_getNumOutcomes(cgraph, node);
                std::vector<ZL_GraphID> dsts(nbSuccessors, ZL_GRAPH_STORE);
                auto graph = ZL_Compressor_registerStaticGraph_fromNode(
                        cgraph, node, dsts.data(), dsts.size());
                return !ZL_GraphID_isValid(graph);
            });
    ZL_Compressor_free(cgraph);

    nodes.resize(end - nodes.begin());

    return nodes;
}

std::vector<ZL_GraphID> getAllGraphs(uint32_t formatVersion)
{
    std::vector<ZL_GraphID> graphs(GR_getNbStandardGraphs());
    GR_getAllStandardGraphIDs(graphs.data(), graphs.size());

    auto cgraph = ZL_Compressor_create();
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, formatVersion));
    auto end = std::remove_if(
            graphs.begin(), graphs.end(), [cgraph](ZL_GraphID graph) {
                if (ZL_Compressor_Graph_getNumInputs(cgraph, graph) != 1) {
                    return true;
                }
                return !ZL_GraphID_isValid(graph);
            });
    ZL_Compressor_free(cgraph);

    graphs.resize(end - graphs.begin());

    return graphs;
}

template <typename F>
size_t findFirstAfter(size_t start, size_t size, F const& fn)
{
    size_t idx = start;
    do {
        if (fn(idx))
            return idx;
        idx = (idx + 1) % size;
    } while (idx != start);
    throw std::runtime_error("No idx is true!");
}

ZL_GraphID buildStoreGraph(ZL_Compressor* cgraph, ZL_Type inType)
{
    if (inType == ZL_Type_string) {
        return ZL_Compressor_registerStaticGraph_fromNode(
                cgraph,
                ZL_NODE_SEPARATE_STRING_COMPONENTS,
                ZL_GRAPHLIST(ZL_GRAPH_STORE, ZL_GRAPH_STORE));
    }
    return ZL_GRAPH_STORE;
}

ZL_GraphID buildGraph(
        datagen::DataGen& dg,
        ZL_Compressor* cgraph,
        size_t* nodesInGraph,
        std::vector<ZL_NodeID> const& nodes,
        std::vector<ZL_GraphID> const& graphs,
        ZL_Type inType,
        size_t maxDepth)
{
    // Stop at kMaxNodesInGraph to avoid running out of space
    // in ZStrongs fixed size arrays
    if (*nodesInGraph > kMaxNodesInGraph || maxDepth == 0) {
        return buildStoreGraph(cgraph, inType);
    }

    ++*nodesInGraph;

    // Give some chance to stop the graph with store immediately
    bool const stop = dg.coin("use_store", 0.1);
    if (stop) {
        return buildStoreGraph(cgraph, inType);
    }

    // Choose between a graph or a node
    bool const useGraph = dg.boolean("use_graph");
    if (useGraph) {
        // Pick an index, then pick the first graph after that index
        // that has a compatible type
        size_t graphIdx = dg.usize_range("graph_index", 0, graphs.size() - 1);
        graphIdx = findFirstAfter(graphIdx, graphs.size(), [&](size_t idx) {
            auto const graphType =
                    ZL_Compressor_Graph_getInput0Mask(cgraph, graphs[idx]);
            return ICONV_isCompatible(inType, graphType);
        });
        return graphs[graphIdx];
    }

    // Pick an index, then pick the first node after that index
    // that has a compatible type
    size_t nodeIdx  = dg.usize_range("node_index", 0, nodes.size() - 1);
    nodeIdx         = findFirstAfter(nodeIdx, nodes.size(), [&](size_t idx) {
        auto const nodeType =
                ZL_Compressor_Node_getInput0Type(cgraph, nodes[idx]);
        return ICONV_isCompatible(inType, nodeType);
    });
    auto const node = nodes[nodeIdx];

    // Fill the successor nodes recursively
    size_t const nbSuccessors = ZL_Compressor_Node_getNumOutcomes(cgraph, node);
    std::vector<ZL_GraphID> successors(nbSuccessors);
    for (size_t i = 0; i < successors.size(); ++i) {
        auto const outType = ZL_Compressor_Node_getOutputType(cgraph, node, i);
        successors[i]      = buildGraph(
                dg, cgraph, nodesInGraph, nodes, graphs, outType, maxDepth - 1);
    }

    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, node, successors.data(), successors.size());
}

FUZZ(GraphTest, FuzzGraphRoundTrip)
{
    datagen::DataGen dg   = fromFDP(f);
    ZL_Compressor* cgraph = ZL_Compressor_create();
    ASSERT_NE(cgraph, nullptr);

    // We can't guarantee that our graph is fully valid,
    // because some nodes might not accept all inputs of
    // their type. If that happens, use the fallback graph.
    // This should guarantee that compression always succeeds.
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_permissiveCompression, ZL_TernaryParam_enable));
    // Set the format version to a random version.
    uint32_t const formatVersion = dg.u32_range(
            "format_version", ZL_MIN_FORMAT_VERSION, ZL_MAX_FORMAT_VERSION);
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, formatVersion));

    // Build a random graph
    size_t nodesInGraph    = 0;
    ZL_GraphID const graph = buildGraph(
            dg,
            cgraph,
            &nodesInGraph,
            getAllNodes(formatVersion),
            getAllGraphs(formatVersion),
            ZL_Type_serial,
            kMaxGraphDepth);
    ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph, graph));

    std::string input = dg.randStringWithQuantizedLength("input_str", 1);

    // TODO(terrelln): ZL_compressBound() doesn't provide a tight bound
    // on compressed size. And it is impossible to provide in the general case
    // because we don't have any way of bounding it. So just provide a buffer
    // 10x longer than the maximum input size.
    size_t constexpr kMaxCompressedSize = kDefaultMaxInputLength * 10;
    std::string compressed(kMaxCompressedSize, '\0');

    // Compress the input - it must succeed, unless the destination buffer
    // is too small. We can't bound the compressed size for arbitrary graphs,
    // so just skip the round-trip test if the buffer is too small.
    auto const cSize = ZL_compress_usingCompressor(
            compressed.data(),
            compressed.size(),
            input.data(),
            input.size(),
            cgraph);
    if (ZL_isError(cSize)) {
        auto code = ZL_errorCode(cSize);
        ASSERT_EQ(code, ZL_ErrorCode_dstCapacity_tooSmall);
        ZL_Compressor_free(cgraph);
        return;
    }

    // Decompress the data
    std::string roundTripped(input.size(), '\0');
    auto const dSize = ZL_decompress(
            roundTripped.data(),
            roundTripped.size(),
            compressed.data(),
            ZL_validResult(cSize));
    ZL_REQUIRE_SUCCESS(dSize);

    // Ensure we've round-tripped correctly
    ASSERT_EQ(ZL_validResult(dSize), roundTripped.size());
    ASSERT_TRUE(input == roundTripped);

    ZL_Compressor_free(cgraph);
}
} // namespace
} // namespace tests
} // namespace openzl
