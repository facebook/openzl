// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/utils/pareto_combination.h"

#include <algorithm>
#include <exception>
#include <future>
#include <thread>

#include "tools/logger/Logger.h"
#include "tools/training/sample_collection/training_sample_collector.h"
#include "tools/training/utils/crowding_distance_selector.h"
#include "tools/training/utils/genetic_algorithm.h"
#include "tools/training/utils/utils.h"

namespace openzl {
namespace training {

using namespace openzl::tools::logger;

namespace {

// TODO: Make these hyperparameters training args
const size_t kNumIntermediateFrontierCandidates = 1000;
const size_t kNumFinalParetoCandidates          = 100;

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
    if (currentFrontier.empty()) {
        newFrontier = nextFrontier;
    } else if (nextFrontier.empty()) {
        newFrontier = currentFrontier;
    } else {
        newFrontier.reserve(currentFrontier.size() * nextFrontier.size());
        for (const auto& candidate : currentFrontier) {
            for (const auto& candidateToMerge : nextFrontier) {
                auto newCandidate = candidate;
                newCandidate.merge(candidateToMerge);
                newFrontier.emplace_back(std::move(newCandidate));
            }
        }
        newFrontier = filterParetoFrontier(std::move(newFrontier), threadPool);
    }
    newFrontier = pruneCandidates(std::move(newFrontier), maxNumCandidates);
    return newFrontier;
}

using BenchmarkFuture = std::future<poly::optional<CompressionResult>>;

/**
 * Waits for every benchmark to finish and @returns their results.
 *
 * Every task must be joined before unwinding, because they reference state
 * owned by the caller, so the first error is rethrown only at the end.
 */
std::vector<std::vector<poly::optional<CompressionResult>>> joinBenchmarks(
        std::vector<std::vector<BenchmarkFuture>> futures,
        size_t totalMutations)
{
    std::vector<std::vector<poly::optional<CompressionResult>>> results;
    results.reserve(futures.size());
    std::exception_ptr error;
    size_t mutationIdx = 0;
    for (auto& graphFutures : futures) {
        std::vector<poly::optional<CompressionResult>> graphResults;
        graphResults.reserve(graphFutures.size());
        for (auto& future : graphFutures) {
            ++mutationIdx;
            Logger::logProgress(
                    INFO,
                    (double)mutationIdx / totalMutations,
                    "Benchmarking candidates: %zu / %zu",
                    mutationIdx,
                    totalMutations);
            try {
                graphResults.push_back(future.get());
            } catch (...) {
                if (error == nullptr) {
                    error = std::current_exception();
                }
                graphResults.push_back(poly::nullopt);
            }
        }
        results.push_back(std::move(graphResults));
    }
    Logger::finalizeUpdate(INFO);
    if (error != nullptr) {
        std::rethrow_exception(error);
    }
    return results;
}

/**
 * Benchmarks every candidate of every backend graph in isolation.
 *
 * @returns For each backend graph, the Pareto-optimal candidates for that graph
 * sorted by compressed size. Backend graphs with no benchmarkable candidate are
 * omitted.
 */
std::vector<std::vector<CandidateSelection>> getBackendGraphSelections(
        const std::function<Compressor()>& makeCompressor,
        const MergedParetoFrontier::BackendGraphMutationsMap& candidates,
        poly::span<const MultiInput> compressionInputs,
        ThreadPool& threadPool)
{
    // Collect the input streams to each backend graph. Sorted so that the
    // frontier doesn't depend on the map's iteration order.
    std::vector<std::string> backendGraphs;
    backendGraphs.reserve(candidates.size());
    size_t totalMutations = 0;
    for (const auto& [name, mutations] : candidates) {
        backendGraphs.push_back(name);
        totalMutations += mutations.size();
    }
    std::sort(backendGraphs.begin(), backendGraphs.end());

    auto compressor         = makeCompressor();
    auto cctx               = refCCtxForTraining(compressor);
    auto backendGraphInputs = collectInputStreamsForGraphs(
            compressionInputs, backendGraphs, cctx);

    // Benchmark every candidate of every backend graph at once, so that graphs
    // with few candidates don't leave the pool idle. The inputs are resolved
    // here because the map must not be indexed from the tasks.
    std::vector<std::vector<BenchmarkFuture>> futures;
    futures.reserve(backendGraphs.size());
    for (const auto& backendGraph : backendGraphs) {
        const auto& inputs = backendGraphInputs[backendGraph];
        std::vector<BenchmarkFuture> graphFutures;
        graphFutures.reserve(candidates.at(backendGraph).size());
        for (const auto& mutation : candidates.at(backendGraph)) {
            graphFutures.push_back(
                    threadPool.run([&makeCompressor, &mutation, &inputs] {
                        return mutation->benchmarkBackendGraph(
                                makeCompressor, inputs);
                    }));
        }
        futures.push_back(std::move(graphFutures));
    }
    const auto results = joinBenchmarks(std::move(futures), totalMutations);

    // Build a CandidateSelection list for each frontier that selects only that
    // one backend graph's candidate.
    std::vector<std::vector<CandidateSelection>> backendGraphSelections;
    backendGraphSelections.reserve(backendGraphs.size());
    for (size_t g = 0; g < backendGraphs.size(); ++g) {
        const auto& backendGraph = backendGraphs[g];

        std::vector<CandidateSelection> selections;
        selections.reserve(results[g].size());
        for (size_t i = 0; i < results[g].size(); ++i) {
            if (!results[g][i].has_value()) {
                continue;
            }
            selections.emplace_back(backendGraph, *results[g][i], i);
        }
        if (selections.empty()) {
            Logger::log(
                    WARNINGS,
                    "No candidate could be benchmarked for backend graph ",
                    backendGraph,
                    ": leaving it unmodified");
            continue;
        }

        // Filter down to the Pareto-optimal frontier & sort
        selections = filterParetoFrontier(std::move(selections), threadPool);
        std::sort(selections.begin(), selections.end());
        backendGraphSelections.push_back(std::move(selections));
    }
    return backendGraphSelections;
}
} // namespace

MergedParetoFrontier::MergedParetoFrontier(
        std::function<Compressor()> makeCompressor,
        BackendGraphMutationsMap candidates,
        poly::span<const MultiInput> inputs,
        poly::optional<uint32_t> threads)
        : makeCompressor_(std::move(makeCompressor)),
          candidates_(std::move(candidates))
{
    ThreadPool threadPool(
            std::max<size_t>(
                    1,
                    threads.value_or(std::thread::hardware_concurrency() / 2)));

    auto selections = getBackendGraphSelections(
            makeCompressor_, candidates_, inputs, threadPool);

    auto frontier = combineCandidates(selections, threadPool);
    paretoFrontier_ =
            pruneCandidates(std::move(frontier), kNumFinalParetoCandidates);
    std::sort(paretoFrontier_.begin(), paretoFrontier_.end());
    if (paretoFrontier_.empty()) {
        paretoFrontier_.emplace_back();
    }
}

std::vector<SerializedCompressorInternal> MergedParetoFrontier::paretoFrontier()
        const
{
    std::vector<SerializedCompressorInternal> compressors;
    compressors.reserve(paretoFrontier_.size());
    for (size_t i = 0; i < paretoFrontier_.size(); ++i) {
        compressors.push_back(serializedAt(i));
    }
    return compressors;
}

std::unordered_map<std::string, const BackendGraphMutation*>
MergedParetoFrontier::mutationsAt(size_t idx) const
{
    const auto& choices = paretoFrontier_.at(idx).choices();
    std::unordered_map<std::string, const BackendGraphMutation*> mutations;
    mutations.reserve(choices.size());
    for (const auto& [backendGraph, choice] : choices) {
        mutations.emplace(
                backendGraph, candidates_.at(backendGraph)[choice].get());
    }
    return mutations;
}

SerializedCompressorInternal MergedParetoFrontier::serializedAt(
        size_t idx) const
{
    auto compressor = makeCompressor_();
    for (const auto& [backendGraph, choice] :
         paretoFrontier_.at(idx).choices()) {
        candidates_.at(backendGraph)[choice]->mutate(compressor);
    }
    return SerializedCompressorInternal(compressor.serialize());
}

/**
 * Selects the least crowded candidates from the given @param candidates.
 */
std::vector<CandidateSelection> pruneCandidates(
        std::vector<CandidateSelection>&& candidates,
        size_t numCandidates)
{
    if (candidates.size() <= numCandidates) {
        return std::move(candidates);
    }

    // Initialize info
    std::vector<std::vector<float>> fitness;
    std::vector<size_t> indices;
    fitness.reserve(candidates.size());
    indices.reserve(candidates.size());
    size_t candidateIdx = 0;
    for (const auto& candidate : candidates) {
        fitness.emplace_back(candidate.asFloatVector());
        indices.emplace_back(candidateIdx++);
    }

    auto crowdingDistances = crowdingDistance(fitness, indices);
    std::vector<CandidateSelection> prunedCandidates;
    prunedCandidates.reserve(numCandidates);
    for (const auto& index :
         selectLeastCrowded(fitness, crowdingDistances, numCandidates)) {
        prunedCandidates.emplace_back(std::move(candidates[index]));
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
    // Every task reads every candidate, so all of them must complete before any
    // candidate is moved out of.
    std::vector<bool> isDominated;
    isDominated.reserve(candidates.size());
    for (auto& future : futures) {
        isDominated.push_back(future.get());
    }
    for (size_t i = 0; i < candidates.size(); i++) {
        if (!isDominated[i]) {
            frontier.emplace_back(std::move(candidates[i]));
        }
    }
    return frontier;
}

std::vector<CandidateSelection> combineCandidates(
        const std::vector<std::vector<CandidateSelection>>& candidates,
        ThreadPool& threadPool)
{
    size_t count = 0;
    std::vector<CandidateSelection> currentFrontier;
    for (const auto& candidate : candidates) {
        count++;
        Logger::logProgress(
                INFO,
                (double)count / candidates.size(),
                "Computing overall Pareto Frontier: %zu / %zu",
                count,
                candidates.size());
        currentFrontier = mergeParetoFrontier(
                threadPool,
                currentFrontier,
                candidate,
                kNumIntermediateFrontierCandidates);
    }
    Logger::finalizeUpdate(INFO);
    return currentFrontier;
}

} // namespace training
} // namespace openzl
