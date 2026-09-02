// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/poly/Optional.hpp"
#include "openzl/cpp/poly/Span.hpp"

#include "tools/training/train_params.h"
#include "tools/training/utils/benchmark.h"
#include "tools/training/utils/mutation.h"
#include "tools/training/utils/serialized_compressor_internal.h"
#include "tools/training/utils/thread_pool.h"

namespace openzl {
namespace training {

/**
 * One choice of candidate mutation per backend graph, together with the
 * combined benchmark result those choices are expected to produce.
 */
class CandidateSelection {
   public:
    /// Selects nothing, which represents leaving the compressor unmodified.
    CandidateSelection() = default;

    CandidateSelection(
            const std::string& name,
            const CompressionResult& result,
            size_t index)
            : mergedResult_(result)
    {
        choices_[name] = index;
    }

    bool operator<(const CandidateSelection& other) const
    {
        return mergedResult_ < other.mergedResult_;
    }

    /**
     * @returns true if strictly dominates @p other
     */
    bool dominates(const CandidateSelection& other) const
    {
        return mergedResult_.dominates(other.mergedResult_);
    }

    /**
     * Adds all choices from the candidate @p toMerge to the map as well as
     * adding the total time taken and compressed size of the associated
     * sub-compressors
     */
    void merge(const CandidateSelection& toMerge)
    {
        for (const auto& choice : toMerge.choices_) {
            if (choices_.count(choice.first) != 0) {
                throw Exception(
                        "Subcompressor in candidate to merge has already been chosen");
            }
            choices_.emplace(choice);
        }
        mergedResult_ += toMerge.mergedResult_;
    }

    /**
     * Computes the fitness based on size and times.
     */
    std::vector<float> asFloatVector() const
    {
        return mergedResult_.asFloatVector();
    }

    const CompressionResult& result() const
    {
        return mergedResult_;
    }

    const std::unordered_map<std::string, size_t>& choices() const
    {
        return choices_;
    }

   private:
    // A mapping from backend graph name to the index of the chosen candidate
    // mutation for that graph
    std::unordered_map<std::string, size_t> choices_;
    // The combined compression ratio/ speeds the
    // combined compressor is expected to produce
    CompressionResult mergedResult_;
};

/**
 * Takes a set of candidate mutations for each backend graph and produces the
 * Pareto-optimal set of compressors obtained by choosing exactly one candidate
 * per backend graph.
 *
 * Every candidate is benchmarked in isolation on the inputs its backend graph
 * receives, and the results are combined additively. This assumes that the
 * backend graphs are independent, which lets the combined frontier be computed
 * without benchmarking the combinatorial number of whole compressors.
 *
 * A backend graph whose candidates all fail to benchmark is left unmodified. If
 * no backend graph can be mutated at all, the frontier holds a single selection
 * which leaves the compressor unmodified.
 *
 * @warning Assumes that each backend graph is independent. If one of these
 * graphs forwards to another, the assumptions made here will break.
 */
class MergedParetoFrontier {
   public:
    using BackendGraphMutationsMap = std::unordered_map<
            std::string,
            std::vector<std::unique_ptr<BackendGraphMutation>>>;

    /**
     * @param makeCompressor Creates a new compressor that has processed
     * dependencies, into which the candidate mutations are applied. Candidates
     * are benchmarked in parallel, so it must be safe to call concurrently. It
     * is also retained and called by serializedAt(), so it must stay valid for
     * as long as this object does.
     * @param candidates The candidate mutations to choose between, keyed by
     * backend graph name.
     * @param inputs The samples to benchmark on. These are the inputs to the
     * whole compressor, not to the individual backend graphs.
     */
    MergedParetoFrontier(
            std::function<Compressor()> makeCompressor,
            BackendGraphMutationsMap candidates,
            poly::span<const MultiInput> inputs,
            const TrainParams& params);

    std::vector<SerializedCompressorInternal> paretoFrontier() const;

    poly::span<const CandidateSelection> selections() const
    {
        return paretoFrontier_;
    }

    /// @returns The mutation chosen for each backend graph at @p idx. Backend
    /// graphs which are left unmodified are absent.
    std::unordered_map<std::string, const BackendGraphMutation*> mutationsAt(
            size_t idx) const;

    SerializedCompressorInternal serializedAt(size_t idx) const;

   private:
    std::function<Compressor()> makeCompressor_;
    BackendGraphMutationsMap candidates_;
    std::vector<CandidateSelection> paretoFrontier_;
};

/*******************************
 * Helpers exposed for testing *
 *******************************/

/**
 * Given a vector of choices for each subcompressor, returns the overall pareto
 * frontier obtained from choosing one candidate from each subcompressor.
 */
std::vector<CandidateSelection> combineCandidates(
        const std::vector<std::vector<CandidateSelection>>& candidates,
        ThreadPool& threadPool);

/** Prunes the list of candidates provided in @param candidates based on
 * crowdingDistance and returns it. Picks the  @param numCandidates number of
 * candidates and tries to maximize minimum crowding distance.
 */
std::vector<CandidateSelection> pruneCandidates(
        std::vector<CandidateSelection>&& candidates,
        size_t numCandidates);

/** Filters @param candidates down to its Pareto Frontier and returns it.
 */
std::vector<CandidateSelection> filterParetoFrontier(
        std::vector<CandidateSelection>&& candidates,
        ThreadPool& threadPool);

} // namespace training
} // namespace openzl
