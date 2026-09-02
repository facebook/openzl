// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/lz/lz_trainer.h"

#include <memory>

#include "openzl/openzl.hpp"

#include "openzl/zl_reflection.h"
#include "tools/logger/Logger.h"
#include "tools/training/graph_mutation/graph_mutation_utils.h"
#include "tools/training/train_exceptions.h"
#include "tools/training/utils/pareto_combination.h"

namespace openzl {
namespace training {
const std::string LZ_GRAPH_NAME = "zl.lz";

namespace {
using namespace openzl::training::graph_mutation;
using namespace openzl::tools::logger;

/// A mutation which retunes an LZ backend graph in place.
class LzGraphMutation : public BackendGraphMutation {
   public:
    LzGraphMutation(std::string lzGraph, graphs::Lz::Parameters params)
            : BackendGraphMutation(std::move(lzGraph)), lz_(std::move(params))
    {
    }

    poly::optional<GraphParameters> newGraphParams(Compressor&) const override
    {
        return lz_.parameters();
    }

   private:
    graphs::Lz lz_;
};

/**
 * @returns Every LZ backend graph in @p compressor which can be retuned.
 *
 * Standard graphs and unparameterized graphs cannot have their parameters
 * overridden, so they are skipped.
 */
std::vector<std::string> findLzGraphs(const Compressor& compressor)
{
    std::vector<std::string> lzGraphs;
    for (const auto& graph :
         findAllGraphsWithPrefix(compressor, LZ_GRAPH_NAME)) {
        const auto* name = ZL_Compressor_Graph_getName(compressor.get(), graph);
        if (ZL_Compressor_getGraphType(compressor.get(), graph)
            != ZL_GraphType_parameterized) {
            Logger::log(
                    VERBOSE1,
                    "Skipping LZ graph ",
                    name,
                    ": not a parameterized graph");
            continue;
        }
        lzGraphs.emplace_back(name);
    }
    return lzGraphs;
}

/**
 * @returns The parameters to try for a single LZ backend graph.
 *
 * TODO: Switch to use a genetic algorithm rather than brute force.
 * We're currently not searching hashLog1 and hashLog2 because it would
 * expand the search space to be unreasonably large. This is expected to
 * get the majority of the interesting search space, however.
 */
std::vector<graphs::Lz::Parameters> candidateParameters()
{
    const std::vector<poly::optional<int>> levels = {
        poly::nullopt, { 1 }, { 2 }, { 3 }, { 4 },
    };
    const std::vector<poly::optional<int>> hashLengths = {
        poly::nullopt, { 4 }, { 5 }, { 6 }, { 7 },
    };
    const std::vector<poly::optional<int>> accelerations = {
        poly::nullopt, { 2 }, { 3 }, { 5 }, { 7 },
    };
    const std::vector<poly::optional<int>> windowLogs = {
        poly::nullopt,
        { 16 },
    };
    const std::vector<poly::optional<GraphID>> literalsGraphs = {
        poly::nullopt,
        { ZL_GRAPH_STORE },
        { ZL_GRAPH_FLATPACK },
    };
    const std::vector<poly::optional<GraphID>> offsetsGraphs = {
        poly::nullopt,
        { ZL_GRAPH_STORE },
        { ZL_GRAPH_BITPACK },
    };
    const std::vector<poly::optional<GraphID>> muxedBytesGraphs = {
        poly::nullopt,
        { ZL_GRAPH_STORE },
        { ZL_GRAPH_FLATPACK },
    };
    const std::vector<poly::optional<GraphID>> overflowLengthsGraphs = {
        poly::nullopt,
        { ZL_GRAPH_STORE },
        { ZL_GRAPH_BITPACK },
    };

    std::vector<graphs::Lz::Parameters> candidates;
    for (auto level : levels) {
        for (auto acceleration : accelerations) {
            for (auto windowLog : windowLogs) {
                for (auto hashLength : hashLengths) {
                    for (auto literalsGraph : literalsGraphs) {
                        for (auto offsetsGraph : offsetsGraphs) {
                            for (auto muxedBytesGraph : muxedBytesGraphs) {
                                for (auto overflowLengthsGraph :
                                     overflowLengthsGraphs) {
                                    graphs::Lz::Parameters params;
                                    params.nodeParams.compressionLevel = level;
                                    params.nodeParams.acceleration =
                                            acceleration;
                                    params.nodeParams.windowLog  = windowLog;
                                    params.nodeParams.hashLength = hashLength;
                                    params.literalsGraph   = literalsGraph;
                                    params.offsetsGraph    = offsetsGraph;
                                    params.muxedBytesGraph = muxedBytesGraph;
                                    params.overflowLengthsGraph =
                                            overflowLengthsGraph;
                                    candidates.push_back(std::move(params));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return candidates;
}

/// @returns A mutation of @p lzGraph for each set of parameters to try.
std::vector<std::unique_ptr<BackendGraphMutation>> trainLzGraph(
        const std::string& lzGraph)
{
    auto parameters = candidateParameters();
    std::vector<std::unique_ptr<BackendGraphMutation>> mutations;
    mutations.reserve(parameters.size());
    for (auto& params : parameters) {
        mutations.push_back(
                std::make_unique<LzGraphMutation>(lzGraph, std::move(params)));
    }
    return mutations;
}

} // namespace

void LzTrainer::train(
        poly::span<const MultiInput> inputs,
        poly::string_view serializedCompressorInput,
        const TrainParams& trainParams)
{
    // The frontier outlives train(), and calls this to rebuild the compressor,
    // so it must own everything it needs.
    auto makeCompressor = [serialized = std::string(serializedCompressorInput),
                           compressorGenFunc = trainParams.compressorGenFunc] {
        return std::move(*compressorGenFunc(serialized, ""));
    };

    auto compressor = makeCompressor();

    // 1. Find LZ graph instances
    const auto lzGraphs = findLzGraphs(compressor);
    // Check format version is sufficient for LZ training
    if (!lzGraphs.empty()) {
        const auto formatVersion =
                compressor.getParameter(CParam::FormatVersion);
        const int minFormatVersion =
                ZL_Compressor_Node_getMinVersion(compressor.get(), ZL_NODE_LZ);
        if (formatVersion < minFormatVersion) {
            throw FormatVersionUnsupportedError(
                    "LZ training requires format version >= "
                    + std::to_string(minFormatVersion) + "; target version "
                    + std::to_string(formatVersion) + " is unsupported");
        }
    }
    Logger::log(
            VERBOSE1, "Found ", lzGraphs.size(), " LZ graphs in compressor");

    // 2. Train each LZ graph instance
    MergedParetoFrontier::BackendGraphMutationsMap candidates;
    candidates.reserve(lzGraphs.size());
    for (const auto& lzGraph : lzGraphs) {
        candidates.emplace(lzGraph, trainLzGraph(lzGraph));
    }

    // 3. Combine the Pareto Frontiers
    paretoFrontier_.emplace(
            std::move(makeCompressor),
            std::move(candidates),
            inputs,
            trainParams);
}

std::vector<SerializedCompressorInternal> LzTrainer::paretoFrontier() const
{
    if (!paretoFrontier_) {
        throw Exception("Must call LzTrainer::train() first");
    }
    return paretoFrontier_->paretoFrontier();
}

} // namespace training
} // namespace openzl
