// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/dev/contrib/gpu/testkit/frame_factory.h"

#include <iterator>
#include <vector>

#include "openzl/zl_common_types.h" // ZL_TernaryParam_disable
#include "openzl/zl_compressor.h" // ZL_Compressor_registerStaticGraph_fromNode1o
#include "tests/utils.h"          // buildTrivialGraph

namespace openzl::gpu::testkit {

namespace {

// MinStreamSize follows the convention that 0 means "default"; only a NEGATIVE
// threshold fully disables the per-stream "automatic store" feature. -1 is the
// smallest such value.
constexpr int kDisableMinStreamSize = -1;

// Applies the parameters that keep hand-picked codecs intact:
//   FormatVersion     -- pin the wire format so reflection IDs are stable.
//   StoreOnExpansion  -- off, so a chunk that fails to shrink is NOT swapped
//                        wholesale for a single raw STORE. This is the
//                        load-bearing one: an ablation on float_deconstruct ->
//                        {store, store} showed that WITHOUT it the transform is
//                        backed up and codec 33 vanishes (only convert+store
//                        remain), and WITH it codec 33 survives.
//   MinStreamSize     -- negative, so individual small streams are not silently
//                        stored before reaching the backend we routed them to.
//                        Belt-and-suspenders here: it did not affect the
//                        float_deconstruct case alone, but matters for graphs
//                        with small intermediate streams feeding real backends.
void applyFidelityParams(openzl::CCtx& cctx)
{
    cctx.setParameter(openzl::CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    cctx.setParameter(
            openzl::CParam::StoreOnExpansion, ZL_TernaryParam_disable);
    cctx.setParameter(openzl::CParam::MinStreamSize, kDisableMinStreamSize);
}

size_t unguardedCompressBound(const openzl::Input& input)
{
    size_t inputSize = input.contentSize();
    if (input.type() == openzl::Type::String) {
        inputSize += input.numElts() * sizeof(uint32_t);
    }
    return ZL_COMPRESSBOUND_UNGUARDED(inputSize);
}

} // namespace

std::string makeFrameWithGraph(
        openzl::Compressor& compressor,
        openzl::GraphID startGraph,
        const openzl::Input& input)
{
    compressor.selectStartingGraph(startGraph);

    openzl::CCtx cctx;
    cctx.refCompressor(compressor);
    applyFidelityParams(cctx);
    std::string frame(unguardedCompressBound(input), '\0');
    frame.resize(
            cctx.compress(frame, poly::span<const openzl::Input>(&input, 1)));
    return frame;
}

std::string makeFrameNodeToStore(
        openzl::Compressor& compressor,
        openzl::NodeID node,
        const openzl::Input& input)
{
    const openzl::GraphID graph =
            tests::buildTrivialGraph(compressor.get(), node);
    return makeFrameWithGraph(compressor, graph, input);
}

std::string makeFrameNodeWithSuccessors(
        openzl::Compressor& compressor,
        openzl::NodeID node,
        std::initializer_list<openzl::GraphID> successors,
        const openzl::Input& input)
{
    const openzl::GraphID graph = compressor.buildStaticGraph(node, successors);
    return makeFrameWithGraph(compressor, graph, input);
}

std::string makeFrameChainToStore(
        openzl::Compressor& compressor,
        std::initializer_list<openzl::NodeID> chain,
        const openzl::Input& input)
{
    // Build the single-output chain from the tail backwards: the last node
    // feeds STORE, and each earlier node feeds the graph built so far.
    openzl::GraphID graph = openzl::graphs::Store{}();
    for (auto it = std::rbegin(chain); it != std::rend(chain); ++it) {
        graph = ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor.get(), *it, graph);
    }
    return makeFrameWithGraph(compressor, graph, input);
}

bool decompressedEquals(const std::string& frame, const openzl::Input& input)
{
    openzl::DCtx dctx;
    const std::vector<openzl::Output> outs = dctx.decompress(frame);
    return outs.size() == 1 && input == outs[0];
}

bool roundTrips(
        openzl::Compressor& compressor,
        openzl::GraphID startGraph,
        const openzl::Input& input)
{
    const std::string frame = makeFrameWithGraph(compressor, startGraph, input);
    return decompressedEquals(frame, input);
}

} // namespace openzl::gpu::testkit
