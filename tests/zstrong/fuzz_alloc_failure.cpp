// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/encoder_registry.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/compress/graph_registry.h"
#include "openzl/compress/implicit_conversion.h"
#include "openzl/openzl.hpp"
#include "openzl/zl_common_types.h" // ZL_TernaryParam_enable
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_reflection.h"
#include "security/lionhead/utils/lib_ftest/ftest.h"
#include "tests/constants.h"
#include "tests/fuzz_utils.h"

#include <algorithm>

namespace zstrong {
namespace tests {
namespace {

std::vector<ZL_NodeID> getAllNodes(uint32_t formatVersion)
{
    std::vector<ZL_NodeID> nodes(ER_getNbStandardNodes());
    ER_getAllStandardNodeIDs(nodes.data(), nodes.size());

    auto cgraph = ZL_Compressor_create();
    if (cgraph == nullptr) {
        return {};
    }
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, formatVersion));
    auto end = std::remove_if(
            nodes.begin(), nodes.end(), [cgraph](ZL_NodeID node) {
                size_t const nbSuccessors =
                        ZL_Compressor_Node_getNumOutcomes(cgraph, node);
                std::vector<ZL_GraphID> dsts(nbSuccessors, ZL_GRAPH_STORE);
                auto graph = ZL_Compressor_registerStaticGraph_fromNode(
                        cgraph, node, dsts.data(), dsts.size());
                if (graph.gid == ZL_GRAPH_ILLEGAL.gid) {
                    return true;
                }
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
    if (cgraph == nullptr) {
        return {};
    }
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            cgraph, ZL_CParam_formatVersion, formatVersion));
    auto end = std::remove_if(
            graphs.begin(), graphs.end(), [cgraph](ZL_GraphID graph) {
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
    return (size_t)-1;
}

template <typename FDP>
ZL_GraphID buildGraph(
        FDP& f,
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
        return ZL_GRAPH_STORE;
    }

    ++*nodesInGraph;

    // Give some chance to stop the graph with store immediately
    bool const stop = f.coin("use_store", 0.1)
            || (graphs.size() == 0 && nodes.size() == 0);
    if (stop) {
        return ZL_GRAPH_STORE;
    }

    // Choose between a graph or a node
    bool const useGraph =
            (f.boolean("use_graph") && graphs.size() != 0) || nodes.size() == 0;
    if (useGraph) {
        // Pick an index, then pick the first graph after that index
        // that has a compatible type
        size_t graphIdx = f.index("graph_index", graphs.size());
        graphIdx = findFirstAfter(graphIdx, graphs.size(), [&](size_t idx) {
            auto const graphType =
                    ZL_Compressor_Graph_getInput0Mask(cgraph, graphs[idx]);
            return ICONV_isCompatible(inType, graphType);
        });
        if (graphIdx == (size_t)-1) {
            return ZL_GRAPH_STORE;
        }
        return graphs[graphIdx];
    }

    // Pick an index, then pick the first node after that index
    // that has a compatible type
    size_t nodeIdx = f.index("node_index", nodes.size());
    nodeIdx        = findFirstAfter(nodeIdx, nodes.size(), [&](size_t idx) {
        auto const nodeType =
                ZL_Compressor_Node_getInput0Type(cgraph, nodes[idx]);
        return ICONV_isCompatible(inType, nodeType);
    });
    if (nodeIdx == (size_t)-1) {
        return ZL_GRAPH_STORE;
    }
    auto const node = nodes[nodeIdx];

    // Fill the successor nodes recursively
    size_t const nbSuccessors = ZL_Compressor_Node_getNumOutcomes(cgraph, node);
    std::vector<ZL_GraphID> successors(nbSuccessors);
    for (size_t i = 0; i < successors.size(); ++i) {
        auto const outType = ZL_Compressor_Node_getOutputType(cgraph, node, i);
        successors[i]      = buildGraph(
                f, cgraph, nodesInGraph, nodes, graphs, outType, maxDepth - 1);
    }

    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, node, successors.data(), successors.size());
}

struct AllocState {
    explicit AllocState() : AllocState(0, 0) {}
    explicit AllocState(size_t failEveryNAllocs_, size_t numAllocsOffset_)
            : failEveryNAllocs(failEveryNAllocs_),
              numAllocsOffset(numAllocsOffset_)
    {
    }

    bool operator()()
    {
        ++numAllocs;
        ZL_REQUIRE_NE(numAllocs, crashOnAllocationX);
        if (failEveryNAllocs != 0
            && (numAllocsOffset + numAllocs) % failEveryNAllocs == 0) {
            ++numAllocsFailed;
            ZL_LOG_IF(
                    logOnAllocFail,
                    ALWAYS,
                    "Logging where allocation failed (alloc %zu, fail every %zu)!",
                    numAllocs,
                    failEveryNAllocs);
            ZL_REQUIRE(!crashOnAllocFail, "Crashing where allocation failed");
            return true;
        } else {
            return false;
        }
    }

    size_t failEveryNAllocs;
    size_t numAllocsOffset;
    size_t numAllocs{ 0 };
    size_t numAllocsFailed{ 0 };

    bool logOnAllocFail{ false };
    bool crashOnAllocFail{ false };
    size_t crashOnAllocationX{ 0 };
};

AllocState gAllocState{};

extern "C" bool ZS2_malloc_should_fail(size_t size)
{
    return gAllocState();
}

FUZZ(AllocFailureTest, FuzzAllocFailure)
{
    gAllocState = AllocState{};
    // declare these before allocation failures are toggled
    openzl::Compressor compressor;
    openzl::CCtx myCctx;

    bool allowAllocFailuresInConstruction =
            f.coin("allow_alloc_failures_in_graph_construction", 0.1);
    auto localAllocState =
            AllocState{ f.u32_range("fail_every_n_allocs", 1, 10000),
                        f.u32_range("num_allocs_offset", 0, 10000) };
    if (allowAllocFailuresInConstruction) {
        // Focus fuzzing energy on fuzzing (de)compression, rather than
        // allocation failures during graph construction.
        gAllocState = localAllocState;
    }

    // We can't guarantee that our graph is fully valid,
    // because some nodes might not accept all inputs of
    // their type. If that happens, use the fallback graph.
    // This should guarantee that compression always succeeds.
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            compressor.get(),
            ZL_CParam_permissiveCompression,
            ZL_TernaryParam_enable));
    // Set the format version to a random version.
    uint32_t const formatVersion = f.u32_range(
            "format_version", ZL_MIN_FORMAT_VERSION, ZL_MAX_FORMAT_VERSION);
    ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
            compressor.get(), ZL_CParam_formatVersion, formatVersion));

    // Build a random graph
    size_t nodesInGraph    = 0;
    ZL_GraphID const graph = buildGraph(
            f,
            compressor.get(),
            &nodesInGraph,
            getAllNodes(formatVersion),
            getAllGraphs(formatVersion),
            ZL_Type_serial,
            kMaxGraphDepth);
    if (ZL_isError(
                ZL_Compressor_selectStartingGraphID(compressor.get(), graph))) {
        ZL_REQUIRE(allowAllocFailuresInConstruction);
        ZL_REQUIRE_GT(gAllocState.numAllocsFailed, 0);
        return;
    }

    if (!allowAllocFailuresInConstruction) {
        gAllocState = std::move(localAllocState);
    }

    std::string input = gen_str(f, "input_str", InputLengthInBytes(1));

    // TODO(terrelln): ZL_compressBound() doesn't provide a tight bound
    // on compressed size. And it is impossible to provide in the general case
    // because we don't have any way of bounding it. So just provide a buffer
    // 10x longer than the maximum input size.
    size_t constexpr kMaxCompressedSize = kDefaultMaxInputLength * 10;
    std::string compressed(kMaxCompressedSize, '\0');

    // Compress the input - it must succeed
    myCctx.refCompressor(compressor);
    auto const cSize = ZL_CCtx_compress(
            myCctx.get(),
            compressed.data(),
            compressed.size(),
            input.data(),
            input.size());
    if (ZL_isError(cSize)) {
        // An allocation failure must have caused this because permissive mode
        // is enabled. Ensure we get an appropriate error code.
        ZL_REQUIRE_GT(gAllocState.numAllocsFailed, 0);
        auto code = ZL_errorCode(cSize);
        // We currently can't reliably differentiate containers running into
        // size limits vs. allocation failures
        ZL_REQUIRE(
                code == ZL_ErrorCode_allocation
                || code == ZL_ErrorCode_temporaryLibraryLimitation);
        return;
    }

    // Decompress the data
    std::string roundTripped(input.size(), '\0');
    auto dSize = ZL_decompress(
            roundTripped.data(),
            roundTripped.size(),
            compressed.data(),
            ZL_validResult(cSize));
    if (ZL_isError(dSize)) {
        ZL_REQUIRE_GT(gAllocState.numAllocsFailed, 0);
        // ZL_REQUIRE_EQ((int)ZL_errorCode(dSize),
        // (int)ZL_ErrorCode_allocation);

        // Ensure round trip succeeds if allocations work
        gAllocState = AllocState{};
        dSize       = ZL_decompress(
                roundTripped.data(),
                roundTripped.size(),
                compressed.data(),
                ZL_validResult(cSize));
        ZL_REQUIRE_SUCCESS(dSize);
    }

    // Ensure we've round-tripped correctly
    ZL_REQUIRE_EQ(ZL_validResult(dSize), roundTripped.size());
    ZL_REQUIRE(input == roundTripped);
}
} // namespace
} // namespace tests
} // namespace zstrong
