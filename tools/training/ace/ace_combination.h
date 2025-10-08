// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <functional>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

#include "openzl/cpp/Compressor.hpp"

#include "tools/training/ace/ace_compressor.h"
#include "tools/training/train_params.h"
#include "tools/training/utils/thread_pool.h"

namespace openzl::training {

class CandidateSelection {
   public:
    CandidateSelection(
            const std::string& name,
            const ACECompressionResult& result,
            size_t index)
    {
        choices_[name] = index;
        mergedResult_  = result;
    }

    bool operator<(const CandidateSelection& other) const
    {
        return mergedResult_ < other.mergedResult_;
    }

    /** Returns true if strictly dominates @p other. The fitness parameters in
     * ACECompressionResult are compared against @p other.
     */
    bool dominates(const CandidateSelection& other) const
    {
        if (mergedResult_.compressedSize == other.mergedResult_.compressedSize
            && mergedResult_.compressionTime
                    == other.mergedResult_.compressionTime
            && mergedResult_.decompressionTime
                    == other.mergedResult_.decompressionTime) {
            return false;
        }
        return mergedResult_.compressedSize
                <= other.mergedResult_.compressedSize
                && mergedResult_.compressionTime
                <= other.mergedResult_.compressionTime
                && mergedResult_.decompressionTime
                <= other.mergedResult_.decompressionTime;
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
    std::vector<float> fitness() const
    {
        std::vector<float> fitness(3);
        fitness[0] = mergedResult_.compressedSize;
        fitness[1] = mergedResult_.compressionTime.count();
        fitness[2] = mergedResult_.decompressionTime.count();
        return fitness;
    }

    std::unordered_map<std::string, size_t> choices() const
    {
        return choices_;
    }

   private:
    // A mapping from subCompressor name to the index of the
    // chosen compressor
    std::unordered_map<std::string, size_t> choices_;
    // The combined compression ratio/ speeds the
    // combined compressor is expected to produce
    ACECompressionResult mergedResult_;
};
/**
 * Takes the Pareto Frontier of solutions for all sub-compressor, and produces a
 * Pareto-optimal vector of solutions for the entire compressor. Returns each
 * solution as a serialized compressor.
 *
 * @param makeCompressor A function used to create new compressors that have
 * processed dependencies.
 * @param allCandidates A map of sub-compressor names to the vector of
 * subcompressors and their benchmarks.
 * @param trainParams The training parameters to use for the algorithm.
 */
std::vector<std::shared_ptr<const std::string_view>> getCombinedCompressors(
        const std::function<Compressor()>& makeCompressor,
        const std::unordered_map<
                std::string,
                std::vector<std::pair<ACECompressor, ACECompressionResult>>>&
                allCandidates,
        const TrainParams& trainParams);

/**
 * Given a vector of choices for each subcompressor, returns the overall pareto
 * frontier obtained from choosing one candidate from each subcompressor.
 */
std::vector<CandidateSelection> combineCandidates(
        const std::vector<std::vector<CandidateSelection>>& candidates,
        const TrainParams& trainParams);

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
} // namespace openzl::training
