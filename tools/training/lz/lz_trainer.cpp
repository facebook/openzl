// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/lz/lz_trainer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>

#include "openzl/openzl.hpp"

#include "openzl/zl_reflection.h"
#include "tools/logger/Logger.h"
#include "tools/training/graph_mutation/graph_mutation_utils.h"
#include "tools/training/lz/lz_compressor.h"
#include "tools/training/lz/lz_genetic_algorithm.h"
#include "tools/training/sample_collection/training_sample_collector.h"
#include "tools/training/train_exceptions.h"
#include "tools/training/utils/pareto_combination.h"

namespace openzl {
namespace training {
const std::string LZ_GRAPH_NAME = "zl.lz";

namespace {
using namespace openzl::training::graph_mutation;
using namespace openzl::tools::logger;

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
 * @throws a FormatVersionUnsupportedError if @p compressor targets a format
 * version which predates the LZ node.
 */
void checkFormatVersion(const Compressor& compressor)
{
    const auto formatVersion = compressor.getParameter(CParam::FormatVersion);
    const int minFormatVersion =
            ZL_Compressor_Node_getMinVersion(compressor.get(), ZL_NODE_LZ);
    if (formatVersion < minFormatVersion) {
        throw FormatVersionUnsupportedError(
                "LZ training requires format version >= "
                + std::to_string(minFormatVersion) + "; target version "
                + std::to_string(formatVersion) + " is unsupported");
    }
}

using Deadline = poly::optional<std::chrono::steady_clock::time_point>;

/// @returns The search configuration described by @p trainParams, apart from
/// the time budget, which train() shares out between the LZ graphs.
LzGeneticAlgorithm::Parameters lzParameters(const TrainParams& trainParams)
{
    LzGeneticAlgorithm::Parameters params;
    params.numThreads = trainParams.threads.value_or(
            std::thread::hardware_concurrency() / 2);
    params.dictBundleData = trainParams.dictBundleData;
    return params;
}

/// Runs @p ga to completion, reporting its progress.
void runTraining(LzGeneticAlgorithm& ga, size_t graphIdx, size_t numGraphs)
{
    for (;;) {
        Logger::logProgress(
                INFO,
                ga.progress(),
                "Training LZ graph %zu / %zu: progress",
                graphIdx,
                numGraphs);
        if (ga.finished()) {
            break;
        }
        ga.step();
    }
    Logger::finalizeProgress(INFO);
}

/**
 * @returns A mutation for each tuning on @p ga's Pareto frontier, best
 * compression ratio first, or only the best one when @p paretoFrontier is
 * false.
 *
 * Tunings which failed to compress are dropped.
 */
std::vector<std::unique_ptr<BackendGraphMutation>> makeLzMutations(
        const LzGeneticAlgorithm& ga,
        bool paretoFrontier)
{
    std::vector<std::unique_ptr<BackendGraphMutation>> mutations;
    for (auto&& [tuning, fitness] : ga.solution()) {
        if (std::isinf(fitness[0])) {
            continue;
        }
        mutations.push_back(
                std::make_unique<LzGraphMutation>(
                        ga.backendGraph(), std::move(tuning)));
        if (!paretoFrontier) {
            break;
        }
    }
    return mutations;
}

} // namespace

poly::optional<std::chrono::seconds> timeShare(
        const Deadline& deadline,
        size_t graphsRemaining)
{
    if (!deadline.has_value()) {
        return poly::nullopt;
    }
    const auto remaining = deadline.value() - std::chrono::steady_clock::now();
    const auto share = remaining / (long)std::max<size_t>(1, graphsRemaining);
    // Give every graph a second even once the deadline has passed, so that the
    // last graphs are still tuned rather than left at their defaults.
    return std::max(
            std::chrono::seconds(1),
            std::chrono::duration_cast<std::chrono::seconds>(share));
}

void LzTrainer::train(
        poly::span<const MultiInput> inputs,
        poly::string_view serializedCompressorInput,
        const TrainParams& trainParams)
{
    Deadline deadline;
    if (trainParams.maxTimeSecs.has_value()) {
        deadline = std::chrono::steady_clock::now()
                + std::chrono::seconds(*trainParams.maxTimeSecs);
    }

    // The frontier outlives train(), and calls this to rebuild the compressor,
    // so it must own everything it needs.
    auto makeCompressor = [serialized = std::string(serializedCompressorInput),
                           compressorGenFunc = trainParams.compressorGenFunc] {
        return std::move(*compressorGenFunc(serialized, ""));
    };

    auto compressor = makeCompressor();

    // 1. Find LZ graph instances
    const auto lzGraphs = findLzGraphs(compressor);
    if (!lzGraphs.empty()) {
        checkFormatVersion(compressor);
    }
    Logger::log(
            VERBOSE1, "Found ", lzGraphs.size(), " LZ graphs in compressor");

    // 2. Search for a Pareto-optimal tuning of each LZ graph instance
    auto cctx    = refCCtxForTraining(compressor);
    auto samples = collectInputStreamsForGraphs(inputs, lzGraphs, cctx);

    // Graphs which no input reaches, e.g. an optional field absent from the
    // training data, are left at their defaults. They are dropped up front so
    // that they don't get a share of the time budget.
    std::vector<std::string> trainableGraphs;
    trainableGraphs.reserve(lzGraphs.size());
    for (const auto& lzGraph : lzGraphs) {
        if (samples[lzGraph].empty()) {
            Logger::log(
                    VERBOSE1,
                    "Skipping LZ graph ",
                    lzGraph,
                    ": no training samples");
            continue;
        }
        trainableGraphs.push_back(lzGraph);
    }

    MergedParetoFrontier::BackendGraphMutationsMap candidates;
    candidates.reserve(trainableGraphs.size());
    const size_t numGraphs = trainableGraphs.size();
    for (size_t graphIdx = 0; graphIdx < numGraphs; ++graphIdx) {
        const auto& lzGraph = trainableGraphs[graphIdx];

        auto params    = lzParameters(trainParams);
        params.maxTime = timeShare(deadline, numGraphs - graphIdx);

        LzGeneticAlgorithm ga(
                makeCompressor, lzGraph, samples[lzGraph], params);
        runTraining(ga, graphIdx + 1, numGraphs);

        auto mutations = makeLzMutations(ga, trainParams.paretoFrontier);
        if (mutations.empty()) {
            Logger::log(
                    WARNINGS,
                    "No tuning of LZ graph ",
                    lzGraph,
                    " could be benchmarked: leaving it unmodified");
            continue;
        }
        candidates.emplace(lzGraph, std::move(mutations));
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
