// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

#include "openzl/cpp/Compressor.hpp"

#include "openzl/compress/cgraph.h"
#include "openzl/zl_reflection.h"
#include "tools/logger/Logger.h"
#include "tools/training/ace/ace.h"
#include "tools/training/ace/ace_compressor.h"
#include "tools/training/ace/ace_compressors.h"
#include "tools/training/ace/automated_compressor_explorer.h"
#include "tools/training/graph_mutation/graph_mutation_utils.h"
#include "tools/training/sample_collection/training_sample_collector.h"
#include "tools/training/utils/mutation.h"
#include "tools/training/utils/pareto_combination.h"
#include "tools/training/utils/serialized_compressor_internal.h"
#include "tools/training/utils/utils.h"

namespace openzl::training {

const std::string ACE_GRAPH_NAME = "zl.ace";

namespace {
using namespace openzl::training::graph_mutation;
using namespace openzl::tools::logger;

class ACEBackendGraphMutation : public BackendGraphMutation {
   public:
    /**
     * @param compressor Replaces the backend graph. Only omitted when
     * @p onlySaveAceState is set.
     * @param aceState Saved onto the backend graph when it is non-null, so that
     * training can later be resumed from it.
     * @param onlySaveAceState Saves @p aceState onto the backend graph but
     * leaves the graph itself alone.
     */
    ACEBackendGraphMutation(
            std::string backendGraph,
            poly::optional<ACECompressor> compressor,
            std::shared_ptr<const std::string> aceState = nullptr,
            bool onlySaveAceState                       = false)
            : BackendGraphMutation(std::move(backendGraph)),
              compressor_(std::move(compressor)),
              aceState_(std::move(aceState)),
              onlySaveAceState_(onlySaveAceState)
    {
        if (onlySaveAceState_) {
            if (!aceState_) {
                throw Exception("ACE state not set when saving the checkpoint");
            }
        } else if (!compressor_.has_value()) {
            throw Exception("No compressor to replace the backend graph with");
        }
    }

    poly::optional<GraphID> newGraph(Compressor& compressor) const override
    {
        if (onlySaveAceState_) {
            return poly::nullopt;
        }
        return compressor_->build(compressor);
    }

    poly::optional<GraphParameters> newGraphParams(
            Compressor& compressor) const override
    {
        if (!aceState_) {
            return {};
        }
        GraphParameters params;
        params.localParams.emplace();
        params.localParams->addCopyParam(
                AutomatedCompressorExplorer::kAceStateParamId,
                aceState_->data(),
                aceState_->size());
        return params;
    }

   private:
    poly::optional<ACECompressor> compressor_;
    std::shared_ptr<const std::string> aceState_;
    bool onlySaveAceState_;
};

/// @returns The names of every ACE backend graph in @p compressor.
std::vector<std::string> findAceGraphs(const Compressor& compressor)
{
    const std::vector<GraphID> graphIDs =
            findAllGraphsWithPrefix(compressor, ACE_GRAPH_NAME);
    std::vector<std::string> graphNames;
    graphNames.reserve(graphIDs.size());
    for (const auto& graphID : graphIDs) {
        graphNames.emplace_back(
                ZL_Compressor_Graph_getName(compressor.get(), graphID));
    }
    return graphNames;
}

/**
 * @returns The ACE state saved on @p backendGraph, or poly::nullopt for the
 * unparameterized graphs which have never been trained.
 */
poly::optional<std::string> getAceState(
        const Compressor& compressor,
        const std::string& backendGraph)
{
    auto backendGraphID = compressor.getGraph(backendGraph);
    if (!backendGraphID.has_value()) {
        throw Exception("Unexpected error: backend graph not found");
    }
    const auto localParams = LocalParams(ZL_Compressor_Graph_getLocalParams(
            compressor.get(), backendGraphID.value()));
    const auto copyParams  = localParams.getCopyParams();
    const auto copyParam   = std::find_if(
            copyParams.begin(), copyParams.end(), [](const auto& param) {
                return param.paramId
                        == AutomatedCompressorExplorer::kAceStateParamId;
            });
    if (copyParam == copyParams.end()) {
        return poly::nullopt;
    }
    return std::string((const char*)copyParam->paramPtr, copyParam->paramSize);
}

/// @returns Every input of every sample in @p samples, referenced in place.
std::vector<Input> flattenSamples(std::vector<MultiInput>& samples)
{
    std::vector<Input> flattened;
    for (auto& sample : samples) {
        for (auto& input : *sample) {
            flattened.push_back(InputRef(input.get()));
        }
    }
    return flattened;
}

/**
 * @returns The compressors on @p ace's Pareto frontier which are able to
 * compress at its format version, best compression ratio first. Only the single
 * best-ratio compressor is returned when @p paretoFrontier is false.
 *
 * Falls back to the store compressor when nothing else can compress.
 */
std::vector<ACECompressor> selectAceCandidates(
        const AutomatedCompressorExplorer& ace,
        bool paretoFrontier)
{
    auto solutions = ace.solution();
    if (solutions.empty()) {
        throw Exception("ACE training failed to find a solution");
    }

    auto inputs = ace.inputs();
    std::vector<ACECompressor> candidates;
    for (auto&& [candidate, _] : solutions) {
        // Drop candidates which cannot compress at the target format version
        if (!candidate.benchmark(inputs, ace.formatVersion()).has_value()) {
            continue;
        }
        candidates.push_back(std::move(candidate));
        if (!paretoFrontier) {
            break;
        }
    }
    if (candidates.empty()) {
        Logger::log(
                WARNINGS,
                "No solution found that meets speed constraints: Falling back to store");
        auto store = buildStoreCompressor();
        if (!store.benchmark(inputs, ace.formatVersion()).has_value()) {
            throw Exception(
                    "Store compressor failed to compress at the target format version");
        }
        candidates.push_back(std::move(store));
    }
    return candidates;
}

/// @returns The ACE run configuration described by @p trainParams.
AutomatedCompressorExplorer::Parameters aceParameters(
        const TrainParams& trainParams,
        uint32_t formatVersion)
{
    AutomatedCompressorExplorer::Parameters params{
        .numThreads = trainParams.threads.value_or(
                std::thread::hardware_concurrency() / 2),
    };
    if (trainParams.maxTimeSecs.has_value()) {
        params.maxTime = std::chrono::seconds(*trainParams.maxTimeSecs);
    }
    params.formatVersion = formatVersion;
    return params;
}

/// Runs @p ace to completion, reporting its progress.
void runTraining(
        AutomatedCompressorExplorer& ace,
        size_t graphIdx,
        size_t numGraphs)
{
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
}

/**
 * @returns A mutation replacing @p backendGraph for each compressor on @p ace's
 * Pareto frontier.
 *
 * @param aceState Saved onto the graph by every mutation when non-null.
 */
std::vector<std::unique_ptr<BackendGraphMutation>> makeAceMutations(
        const std::string& backendGraph,
        const AutomatedCompressorExplorer& ace,
        std::shared_ptr<const std::string> aceState,
        bool paretoFrontier)
{
    auto candidates = selectAceCandidates(ace, paretoFrontier);
    std::vector<std::unique_ptr<BackendGraphMutation>> mutations;
    mutations.reserve(candidates.size());
    for (auto& candidate : candidates) {
        mutations.push_back(
                std::make_unique<ACEBackendGraphMutation>(
                        backendGraph, std::move(candidate), aceState));
    }
    return mutations;
}

/// @returns The compressor produced by applying @p mutations in order.
Compressor applyMutations(
        const std::function<Compressor()>& makeCompressor,
        const std::vector<std::unique_ptr<BackendGraphMutation>>& mutations)
{
    auto compressor = makeCompressor();
    for (const auto& mutation : mutations) {
        mutation->mutate(compressor);
    }
    return compressor;
}

} // namespace

std::vector<SerializedCompressorInternal> ACETrainer::train(
        const std::vector<MultiInput>& inputs,
        std::string_view serializedCompressorInput,
        const TrainParams& trainParams)
{
    auto makeCompressor = [&serializedCompressorInput, &trainParams] {
        return std::move(
                *trainParams.compressorGenFunc(serializedCompressorInput, ""));
    };
    auto compressor = makeCompressor();
    auto cctx       = refCCtxForTraining(compressor);

    const auto autoBackendGraphs = findAceGraphs(compressor);

    Logger::log(
            VERBOSE1,
            "Found ",
            autoBackendGraphs.size(),
            " ACE graphs in compressor");

    auto samples =
            collectInputStreamsForGraphs(inputs, autoBackendGraphs, cctx);
    const auto formatVersion = static_cast<uint32_t>(
            compressor.getParameter(CParam::FormatVersion));

    MergedParetoFrontier::BackendGraphMutationsMap candidates;
    std::vector<std::unique_ptr<BackendGraphMutation>> checkPointMutations;

    size_t graphIdx        = 0;
    const size_t numGraphs = autoBackendGraphs.size();
    for (const auto& backendGraph : autoBackendGraphs) {
        ++graphIdx;
        // Skip graphs with no training samples (e.g., optional fields that
        // aren't present in the training data). These will use default
        // compression.
        auto flattened = flattenSamples(samples[backendGraph]);
        if (flattened.empty()) {
            Logger::log(
                    VERBOSE1,
                    "Skipping ACE graph ",
                    graphIdx,
                    " / ",
                    numGraphs,
                    " (",
                    backendGraph,
                    "): no training samples");
            continue;
        }

        // Graphs which have already been trained resume from their saved
        // population, the rest are trained from scratch.
        const auto savedAceState = getAceState(compressor, backendGraph);
        if (skipTraining_ && !savedAceState.has_value()) {
            Logger::log(
                    VERBOSE1,
                    "Skipping ACE graph ",
                    graphIdx,
                    " / ",
                    numGraphs,
                    " (",
                    backendGraph,
                    "): no ACE state to select from");
            continue;
        }

        AutomatedCompressorExplorer ace(
                flattened, aceParameters(trainParams, formatVersion));
        if (savedAceState.has_value()) {
            ace.loadPopulation(*savedAceState);
        }
        if (!skipTraining_) {
            runTraining(ace, graphIdx, numGraphs);
        }

        // Every candidate for a graph shares that graph's ACE state
        auto aceState =
                std::make_shared<const std::string>(ace.savePopulation());
        checkPointMutations.push_back(
                std::make_unique<ACEBackendGraphMutation>(
                        backendGraph,
                        poly::nullopt,
                        aceState,
                        /* onlySaveAceState */ true));
        candidates.emplace(
                backendGraph,
                makeAceMutations(
                        backendGraph,
                        ace,
                        trainParams.saveAceState ? aceState : nullptr,
                        trainParams.paretoFrontier));
    }

    checkPoint_.emplace(
            applyMutations(makeCompressor, checkPointMutations).serialize());

    MergedParetoFrontier frontier(
            makeCompressor, std::move(candidates), inputs, trainParams);
    return frontier.paretoFrontier();
}

} // namespace openzl::training
