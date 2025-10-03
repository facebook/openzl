// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <memory>
#include <string_view>
#include <vector>

#include "openzl/cpp/Compressor.hpp"

#include "custom_parsers/dependency_registration.h"

#include "tools/logger/Logger.h"
#include "tools/training/ace/ace.h"
#include "tools/training/ace/ace_combination.h"
#include "tools/training/ace/ace_compressor.h"
#include "tools/training/ace/automated_compressor_explorer.h"
#include "tools/training/graph_mutation/graph_mutation_utils.h"
#include "tools/training/sample_collection/training_sample_collector.h"
#include "tools/training/utils/utils.h"

namespace openzl::training {

const std::string ACE_GRAPH_NAME = "zl.ace";

namespace {
using namespace openzl::training::graph_mutation;
using namespace openzl::tools::logger;

/**
 * @returns The Pareto-optimal set of compressors for @p samples.
 */
std::vector<std::pair<ACECompressor, ACECompressionResult>> trainBackend(
        std::vector<MultiInput>& samples,
        const TrainParams& trainParams,
        unsigned graphIdx,
        unsigned numGraphs)
{
    if (samples.empty()) {
        return { { buildCompressGenericCompressor(), ACECompressionResult{} } };
    }
    auto flattened = std::vector<Input>();
    for (auto& sample : samples) {
        for (auto& input : *sample) {
            flattened.push_back(InputRef(input.get()));
        }
    }
    poly::optional<std::chrono::seconds> maxTime;
    if (trainParams.maxTimeSecs.has_value()) {
        maxTime = std::chrono::seconds(trainParams.maxTimeSecs.value());
    } else {
        maxTime = poly::nullopt;
    }
    AutomatedCompressorExplorer::Parameters params{
        .numThreads = trainParams.threads.has_value()
                ? trainParams.threads.value()
                : std::thread::hardware_concurrency() / 2,
    };
    params.maxTime = maxTime;
    AutomatedCompressorExplorer ace(flattened, params);
    for (;;) {
        Logger::logProgress(
                INFO,
                ace.progress(),
                "Training ACE graph %u / %u: ACE progress",
                graphIdx,
                numGraphs);
        if (ace.finished()) {
            break;
        }
        ace.step();
    }
    Logger::finalizeProgress(INFO);
    auto solutions = ace.solution();
    if (solutions.empty()) {
        throw Exception("ACE training failed to find a solution");
    }

    std::vector<std::pair<ACECompressor, ACECompressionResult>> result;
    for (auto&& [candidate, _] : solutions) {
        auto benchmark = *candidate.benchmark(flattened);
        result.emplace_back(std::move(candidate), std::move(benchmark));
        if (!trainParams.paretoFrontier) {
            break;
        }
    }
    if (result.empty()) {
        Logger::log(
                WARNINGS,
                "No solution found that meets speed constraints: Falling back to store");
        auto store = buildStoreCompressor();
        return { { store, *store.benchmark(flattened) } };
    }

    // Register the new graph on the compressor and return the new graph ID
    return result;
}

} // namespace

std::vector<std::shared_ptr<const std::string_view>> trainAceCompressor(
        const std::vector<MultiInput>& inputs,
        std::string_view serializedCompressorInput,
        const TrainParams& trainParams)
{
    auto makeCompressor = [&serializedCompressorInput, &trainParams] {
        return std::move(
                *trainParams.compressorGenFunc(serializedCompressorInput));
    };
    auto compressor = makeCompressor();
    auto cctx       = refCCtxForTraining(compressor);

    // We need to create a new serialized compressor because compressor
    // will have different graph IDs from serializedCompressorInput
    std::string serializedUntrainedCompressor        = compressor.serialize();
    const std::vector<std::string> autoBackendGraphs = findAllGraphsWithPrefix(
            serializedUntrainedCompressor, ACE_GRAPH_NAME);

    if (makeCompressor().serialize() != serializedUntrainedCompressor) {
        // HACK: This is not a strong guarantee that the library provides, so
        // make sure to check it. Ultimately we need the ability to clone
        // compressors.
        throw std::logic_error("Deserialization is not determinsitic!");
    }

    Logger::log(
            VERBOSE1,
            "Found ",
            autoBackendGraphs.size(),
            " ACE graphs in compressor");

    auto samples =
            collectInputStreamsForGraphs(inputs, autoBackendGraphs, cctx);

    std::unordered_map<
            std::string,
            std::vector<std::pair<ACECompressor, ACECompressionResult>>>
            candidates;

    size_t graphIdx        = 0;
    const size_t numGraphs = autoBackendGraphs.size();
    for (const auto& backendGraph : autoBackendGraphs) {
        candidates.emplace(
                backendGraph,
                trainBackend(
                        samples[backendGraph],
                        trainParams,
                        ++graphIdx,
                        numGraphs));
    }
    return getCombinedCompressors(makeCompressor, candidates, trainParams);
}
} // namespace openzl::training
