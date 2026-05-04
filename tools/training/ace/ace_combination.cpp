#include <set>

#include "openzl/compress/cgraph.h"
#include "openzl/zl_reflection.h"
#include "tools/logger/Logger.h"
#include "tools/training/ace/ace_combination.h"
#include "tools/training/ace/ace_compressors.h"
#include "tools/training/ace/automated_compressor_explorer.h"
#include "tools/training/ace/crowding_distance_selector.h"
#include "tools/training/graph_mutation/graph_mutation_utils.h"
#include "tools/training/sample_collection/training_sample_collector.h"
#include "tools/training/utils/genetic_algorithm.h"

namespace openzl::training {

// TODO: Make these hyperparameters training args
const size_t kNumIntermediateFrontierCandidates = 1000;
const size_t kNumFinalParetoCandidates          = 100;
const std::string ACE_GRAPH_NAME                = "zl.ace";

using namespace openzl::tools::logger;
using namespace openzl::training::graph_mutation;

namespace {
/**
 * @returns A serialized compressor of @p compressor where each backend graph is
 * replaced by the given `ACECompressor`.
 */
std::shared_ptr<const std::string_view> runReplacements(
        Compressor& compressor,
        const std::unordered_map<std::string, ACECompressor>& replacements)
{
    // Add each graph to the compressor
    std::unordered_map<std::string, ZL_GraphID> newGraphIds;
    newGraphIds.reserve(replacements.size());
    for (const auto& [backendGraph, aceCompressor] : replacements) {
        newGraphIds.emplace(backendGraph, aceCompressor.build(compressor));
    }

    // Replace each backend graph with the new GraphID
    for (const auto& [backendGraph, newGraphId] : newGraphIds) {
        auto backendGraphId = compressor.getGraph(backendGraph);
        compressor.unwrap(ZL_Compressor_overrideBaseGraph(
                compressor.get(), backendGraphId.value(), newGraphId));
    }

    auto serialized = compressor.serialize();
    auto json       = Compressor::convertSerializedToJson(serialized);
    Logger::log(VERBOSE3, "Graph with trained ACE successors: ", json);

    return graph_mutation::createSharedStringView(std::move(serialized));
}

/**
 * Merges 2 vectors of candidates getting all combinations. Then filters out
 * only pareto optimal points followed by pruning to a limit of the number of
 * candidates.
 */
std::vector<CandidateSelection> mergeParetoFrontier(
        ThreadPool& threadPool,
        const std::vector<CandidateSelection>& currentFrontier,
        const std::vector<CandidateSelection>& nextFrontier,
        size_t maxNumCandidates)
{
    std::vector<CandidateSelection> newFrontier;
    newFrontier.reserve(currentFrontier.size() * nextFrontier.size());
    for (const auto& candidate : currentFrontier) {
        for (const auto& candidateToMerge : nextFrontier) {
            auto newCandidate = candidate;
            newCandidate.merge(candidateToMerge);
            newFrontier.emplace_back(newCandidate);
        }
    }
    newFrontier = filterParetoFrontier(std::move(newFrontier), threadPool);
    newFrontier = pruneCandidates(std::move(newFrontier), maxNumCandidates);
    return newFrontier;
}

/**
 * @returns The compressor for each backend graph that has the best ratio, which
 * is just the first compressor because they are sorted by compressed size.
 */
std::shared_ptr<const std::string_view> getSmallestCandidate(
        const std::function<Compressor()>& makeCompressor,
        const std::unordered_map<
                std::string,
                std::vector<std::pair<ACECompressor, ACECompressionResult>>>&
                allCandidates)
{
    auto compressor = makeCompressor();
    std::unordered_map<std::string, ACECompressor> replacements;
    replacements.reserve(allCandidates.size());
    for (const auto& [backendGraph, candidates] : allCandidates) {
        replacements.emplace(backendGraph, candidates[0].first);
    }
    return runReplacements(compressor, replacements);
}

/**
 * @returns A vector of CandidateSelection constructed from the @p
 * candidateInfo such that one CandidateSelection is produced for each
 * associated compressor.
 */
std::vector<CandidateSelection> candidatesFromVec(
        const std::string& name,
        const std::vector<std::pair<ACECompressor, ACECompressionResult>>&
                candidateInfo)
{
    std::vector<CandidateSelection> candidates;
    size_t candidateIdx = 0;
    candidates.reserve(candidateInfo.size());
    for (const auto& [compressor, result] : candidateInfo) {
        candidates.emplace_back(name, result, candidateIdx++);
    }
    return candidates;
}

/**
 * Requires that a choice has been made for every subcompressor in @param
 * allCandidates for the given @param candidate.
 * @returns the overall serialized compressor from the choices with ACE
 * graphs replaced of @param candidate.
 */
std::shared_ptr<const std::string_view> makeCombinedCompressor(
        const CandidateSelection& candidate,
        const std::function<Compressor()>& makeCompressor,
        const std::unordered_map<
                std::string,
                std::vector<std::pair<ACECompressor, ACECompressionResult>>>&
                allCandidates)
{
    const auto& choices = candidate.choices();
    if (allCandidates.size() != choices.size()) {
        throw Exception("A subcompressor was not chosen for every input.");
    }
    std::unordered_map<std::string, ACECompressor> replacements;
    for (const auto& [name, compressorIdx] : choices) {
        if (allCandidates.count(name) == 0) {
            throw Exception(
                    "The candidate has a name not contained in the map of name to subcompressors.");
        }
        auto compressor = allCandidates.at(name)[compressorIdx].first;
        replacements.emplace(name, compressor);
    }
    auto compressor = makeCompressor();
    return runReplacements(compressor, replacements);
}

std::vector<std::pair<ACECompressor, ACECompressionResult>> benchmarkAce(
        const AutomatedCompressorExplorer& ace,
        const TrainParams& trainParams)
{
    auto solutions = ace.solution();
    if (solutions.empty()) {
        throw Exception("ACE training failed to find a solution");
    }
    // Save this state, do the benchmarking, at the next stages. Call
    // ace.solution() again to kick off the benchmarking.

    auto inputs = ace.inputs();
    std::vector<std::pair<ACECompressor, ACECompressionResult>> result;
    for (auto&& [candidate, _] : solutions) {
        auto benchmark = *candidate.benchmark(inputs);
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
        return { { store, *store.benchmark(inputs) } };
    }

    // Register the new graph on the compressor and return the new graph ID
    return result;
}
} // namespace

/**
 * Selects the least crowded candidates from the given @param candidates.
 */
std::vector<CandidateSelection> pruneCandidates(
        std::vector<CandidateSelection>&& candidates,
        size_t numCandidates)
{
    // Initialize info
    std::vector<std::vector<float>> fitness;
    std::vector<size_t> indices;
    fitness.reserve(candidates.size());
    indices.reserve(candidates.size());
    size_t candidateIdx = 0;
    for (const auto& candidate : candidates) {
        fitness.emplace_back(candidate.fitness());
        indices.emplace_back(candidateIdx++);
    }

    auto crowdingDistances = crowdingDistance(fitness, indices);
    std::vector<CandidateSelection> prunedCandidates;
    prunedCandidates.reserve(numCandidates);
    for (const auto& index :
         selectLeastCrowded(fitness, crowdingDistances, numCandidates)) {
        prunedCandidates.emplace_back(candidates[index]);
    }
    return prunedCandidates;
}

std::vector<CandidateSelection> filterParetoFrontier(
        std::vector<CandidateSelection>&& candidates,
        ThreadPool& threadPool)
{
    // TODO: Filter pareto optimal candidates out in a better way (divide and
    // conquer is O(n log^2 n) as opposed to the current O(n^2) runtime).
    std::vector<std::future<bool>> futures;
    std::vector<CandidateSelection> frontier;
    futures.reserve(candidates.size());
    for (size_t i = 0; i < candidates.size(); i++) {
        auto task = [i, &candidates]() {
            bool isDominated = false;
            for (size_t j = 0; j < candidates.size(); j++) {
                if (candidates[j].dominates(candidates[i])) {
                    isDominated = true;
                    break;
                }
            }
            return isDominated;
        };
        futures.emplace_back(threadPool.run(task));
    }
    for (size_t i = 0; i < candidates.size(); i++) {
        if (!futures[i].get()) {
            frontier.emplace_back(std::move(candidates[i]));
        }
    }
    return frontier;
}

std::vector<CandidateSelection> combineCandidates(
        const std::vector<std::vector<CandidateSelection>>& candidates,
        const TrainParams& trainParams)
{
    ThreadPool threadPool(trainParams.threads.value_or(
            std::thread::hardware_concurrency() / 2));
    size_t count = 0;
    std::vector<CandidateSelection> currentFrontier;
    for (auto& candidate : candidates) {
        count++;
        Logger::logProgress(
                INFO,
                (double)count / candidates.size(),
                "Computing overall Pareto Frontier: %zu / %zu",
                count,
                candidates.size());
        if (currentFrontier.empty()) {
            currentFrontier = candidate;
        } else {
            currentFrontier = mergeParetoFrontier(
                    threadPool,
                    currentFrontier,
                    candidate,
                    kNumIntermediateFrontierCandidates);
        }
    }
    Logger::finalizeUpdate(INFO);
    return currentFrontier;
}

std::vector<std::shared_ptr<const std::string_view>> getCombinedCompressors(
        const std::vector<MultiInput>& inputs,
        std::shared_ptr<const std::string_view> trainedSerializedCompressor,
        const TrainParams& trainParams)
{
    auto makeCompressor = [&trainedSerializedCompressor, &trainParams] {
        return std::move(
                *trainParams.compressorGenFunc(*trainedSerializedCompressor));
    };
    auto compressor = makeCompressor();
    auto cctx       = refCCtxForTraining(compressor);
    auto serialized = compressor.serialize();

    /// Get the ACECompressor for each backend graph from the compressor
    const std::vector<GraphID> autoBackendGraphIDs =
            findAllGraphsWithPrefix(compressor, ACE_GRAPH_NAME);
    std::vector<std::string> autoBackendGraphs;
    autoBackendGraphs.reserve(autoBackendGraphIDs.size());
    for (const auto& graphID : autoBackendGraphIDs) {
        autoBackendGraphs.emplace_back(
                ZL_Compressor_Graph_getName(compressor.get(), graphID));
    }

    // Note this is done a second time (is it worth caching the flattened
    // samples)
    auto samples =
            collectInputStreamsForGraphs(inputs, autoBackendGraphs, cctx);
    std::unordered_map<
            std::string,
            std::vector<std::pair<ACECompressor, ACECompressionResult>>>
            allCandidates;
    for (const auto& backendGraph : autoBackendGraphs) {
        auto backendGraphID = compressor.getGraph(backendGraph);
        if (!backendGraphID.has_value()) {
            throw Exception("Unexpected error: backend graph not found");
        }
        auto localParams = LocalParams(ZL_Compressor_Graph_getLocalParams(
                compressor.get(), backendGraphID.value()));
        auto copyParams  = localParams.getCopyParams();
        auto copyParam   = std::find_if(
                copyParams.begin(), copyParams.end(), [](auto& param) {
                    return param.paramId
                            == AutomatedCompressorExplorer::kAceStateParamId;
                });
        if (copyParam == copyParams.end()) {
            // These are the unparameterized versions without ACE states
            continue;
        }
        auto aceInputs = samples[backendGraph];
        auto flattened = std::vector<Input>();
        for (auto& sample : aceInputs) {
            for (auto& input : *sample) {
                flattened.push_back(InputRef(input.get()));
            }
        }
        if (flattened.empty()) {
            continue;
        }
        auto aceState = std::string(
                (const char*)copyParam->paramPtr, copyParam->paramSize);
        AutomatedCompressorExplorer ace(flattened, aceState);
        auto benchmarks = benchmarkAce(ace, trainParams);
        allCandidates.emplace(backendGraph, std::move(benchmarks));
    }

    if (!trainParams.paretoFrontier) {
        return { getSmallestCandidate(makeCompressor, allCandidates) };
    }
    std::vector<std::vector<CandidateSelection>> candidates;
    candidates.reserve(allCandidates.size());
    for (const auto& [name, subCompressors] : allCandidates) {
        candidates.emplace_back(candidatesFromVec(name, subCompressors));
    }
    auto frontier = combineCandidates(candidates, trainParams);
    frontier = pruneCandidates(std::move(frontier), kNumFinalParetoCandidates);
    std::sort(frontier.begin(), frontier.end());
    std::vector<std::shared_ptr<const std::string_view>> paretoOptimalResults;
    paretoOptimalResults.reserve(frontier.size());
    for (auto& candidate : frontier) {
        paretoOptimalResults.push_back(makeCombinedCompressor(
                candidate, makeCompressor, allCandidates));
    }
    return paretoOptimalResults;
}
} // namespace openzl::training
