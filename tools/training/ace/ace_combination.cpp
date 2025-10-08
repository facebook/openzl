#include <set>

#include "openzl/zl_reflection.h"
#include "tools/logger/Logger.h"
#include "tools/training/ace/ace_combination.h"
#include "tools/training/ace/crowding_distance_selector.h"
#include "tools/training/graph_mutation/graph_mutation_utils.h"
#include "tools/training/utils/genetic_algorithm.h"

namespace openzl::training {

// TODO: Make these hyperparameters training args
const size_t kNumIntermediateFrontierCandidates = 1000;
const size_t kNumFinalParetoCandidates          = 100;

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
    std::string serializedForReplacements = compressor.serialize();
    for (const auto& [backendGraph, newGraphId] : newGraphIds) {
        auto result = replaceBaseGraphInCompressor(
                serializedForReplacements,
                backendGraph,
                ZL_Compressor_Graph_getName(compressor.get(), newGraphId));

        serializedForReplacements = std::string(result.begin(), result.end());
    }

    auto json = Compressor::convertSerializedToJson(serializedForReplacements);
    Logger::log(VERBOSE3, "Graph with trained ACE successors: ", json);

    return graph_mutation::createSharedStringView(
            std::move(serializedForReplacements));
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
        Logger::logProgress(
                INFO,
                (double)count / candidates.size(),
                "Computing overall Pareto Frontier: %zu / %zu",
                count,
                candidates.size());
        count++;
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
    return currentFrontier;
}

std::vector<std::shared_ptr<const std::string_view>> getCombinedCompressors(
        const std::function<Compressor()>& makeCompressor,
        const std::unordered_map<
                std::string,
                std::vector<std::pair<ACECompressor, ACECompressionResult>>>&
                allCandidates,
        const TrainParams& trainParams)
{
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
