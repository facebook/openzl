// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/utils/mutation.h"

#include "openzl/compress/cgraph.h"
#include "tools/training/sample_collection/training_sample_collector.h"

namespace openzl {
namespace training {
GraphID BackendGraphMutation::graph(const Compressor& compressor) const
{
    auto graph = compressor.getGraph(graphName());
    if (!graph.has_value()) {
        throw Exception("Unable to find graph with name: " + name_);
    }
    return graph.value();
}

void BackendGraphMutation::mutate(Compressor& compressor) const
{
    const auto backendGraph    = graph(compressor);
    const auto newBackendGraph = newGraph(compressor);
    bool mutated               = false;
    if (newBackendGraph.has_value()) {
        mutated = true;
        compressor.unwrap(ZL_Compressor_overrideBaseGraph(
                compressor.get(), backendGraph, newBackendGraph.value()));
    }
    const auto newParams = newGraphParams(compressor);
    if (newParams.has_value()) {
        mutated            = true;
        const auto cParams = newParams->toC();
        compressor.unwrap(ZL_Compressor_overrideGraphParams(
                compressor.get(), backendGraph, &cParams));
    }
    if (!mutated) {
        throw Exception("BackendGraphMutation had no mutation!");
    }
}

poly::optional<CompressionResult> BackendGraphMutation::benchmarkBackendGraph(
        const std::function<Compressor()>& makeCompressor,
        poly::span<const MultiInput> backendGraphInputs) const
{
    auto compressor = create(makeCompressor);
    compressor.selectStartingGraph(graph(compressor));
    return training::benchmark(compressor, backendGraphInputs);
}

} // namespace training
} // namespace openzl
