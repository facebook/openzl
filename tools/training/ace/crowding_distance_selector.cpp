// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/ace/crowding_distance_selector.h"
#include "openzl/cpp/Exception.hpp"

#include <set>

namespace openzl::training {

namespace {
class CrowdingDistanceSelector {
   public:
    std::vector<size_t> selectLeastCrowded(
            poly::span<const std::vector<float>> fitness,
            poly::span<const float> crowdingDistance,
            size_t numCandidates)
    {
        init(fitness, crowdingDistance, numCandidates);
        numCandidates = std::min(numCandidates, fitness.size());
        // Remove candidates starting with smallest crowding distance
        while (crowdingDistanceIndexSet_.size() > numCandidates) {
            // Remove the most crowded point from the list
            auto it    = crowdingDistanceIndexSet_.begin();
            auto index = it->second;
            crowdingDistanceIndexSet_.erase(it);
            // For each dimension, the neighbor's crowding distance will be
            // increased by removing it
            for (size_t n = 0; n < nDims_; n++) {
                auto fitnessDim = indexToInfo_[index].fitness[n];
                auto adjFitness = getAdjacentFitness(index, n);
                for (auto adj : adjFitness) {
                    auto delta =
                            std::abs(adj.first - fitnessDim) / dimRanges_[n];
                    updateCrowdingDistance(
                            indexToInfo_[adj.second].crowdingDistance,
                            adj.second,
                            delta);
                }
                // Erase after updating crowding distances
                dimIndexSets_[n].erase({ fitnessDim, index });
            }
        }

        std::vector<size_t> indices;
        indices.reserve(crowdingDistanceIndexSet_.size());
        for (const auto& [_crowdingDistance, index] :
             crowdingDistanceIndexSet_) {
            indices.emplace_back(index);
        }
        return indices;
    }

   private:
    void init(
            poly::span<const std::vector<float>> fitness,
            poly::span<const float> crowdingDistance,
            size_t numCandidates)
    {
        if (fitness.size() == 0) {
            throw Exception("Cannot select candidates with size 0 fitness");
        }
        nDims_ = fitness[0].size();
        if (numCandidates < nDims_ * 2) {
            throw Exception(
                    "Cannot prune candidates that have infinite crowding distance. There must be at least 2 * nDims_ candidates.");
        }
        for (size_t n = 0; n < nDims_; n++) {
            std::set<std::pair<float, size_t>> dimIndexSet;
            for (size_t i = 0; i < fitness.size(); i++) {
                dimIndexSet.emplace(fitness[i][n], i);
            }
            dimIndexSets_.emplace_back(std::move(dimIndexSet));
        }
        indexToInfo_.reserve(fitness.size());
        for (size_t i = 0; i < fitness.size(); i++) {
            crowdingDistanceIndexSet_.emplace(crowdingDistance[i], i);
            indexToInfo_.emplace_back(crowdingDistance[i], fitness[i]);
        }
        // The ranges will not change since crowding distance is infinite at
        // extremities.
        dimRanges_.reserve(nDims_);
        for (size_t n = 0; n < nDims_; n++) {
            dimRanges_.emplace_back(
                    dimIndexSets_[n].rbegin()->first
                    - dimIndexSets_[n].begin()->first);
        }
    }

    std::vector<std::pair<float, size_t>> getAdjacentFitness(
            size_t index,
            size_t dim) const
    {
        std::vector<std::pair<float, size_t>> adjacentFitness;
        auto fitnessDim = indexToInfo_[index].fitness[dim];
        // Since there can be duplicates we must find the one that has
        // matching index
        auto it = dimIndexSets_[dim].find({ fitnessDim, index });
        if (it == dimIndexSets_[dim].end()) {
            throw Exception(
                    "Unexpected algorithm error: matching fitness not found");
        }
        // It is possible for the maximum of a dimension to not have
        // infinite CD when there are duplicates with the same fitness
        // in that dimension
        if (it != dimIndexSets_[dim].begin()) {
            adjacentFitness.emplace_back(*std::prev(it));
        }
        auto next = std::next(it);
        if (next != dimIndexSets_[dim].end()) {
            adjacentFitness.emplace_back(*next);
        }
        return adjacentFitness;
    }

    void updateCrowdingDistance(float oldCD, size_t index, float delta)
    {
        auto it = crowdingDistanceIndexSet_.find({ oldCD, index });
        if (it == crowdingDistanceIndexSet_.end()) {
            throw Exception(
                    "Unexpected algorithm error: crowding distance mismatch");
        }
        crowdingDistanceIndexSet_.erase(it);
        indexToInfo_[index].crowdingDistance += delta;
        crowdingDistanceIndexSet_.emplace(oldCD + delta, index);
    }

    struct CrowdingInfo {
        float crowdingDistance;
        std::vector<float> fitness;

        CrowdingInfo(float cd, std::vector<float> f)
                : crowdingDistance(cd), fitness(std::move(f))
        {
        }
    };

    std::vector<float> dimRanges_;
    size_t nDims_;
    std::set<std::pair<float, size_t>> crowdingDistanceIndexSet_;
    std::vector<std::set<std::pair<float, size_t>>> dimIndexSets_;
    std::vector<CrowdingInfo> indexToInfo_;
};
} // namespace

std::vector<size_t> selectLeastCrowded(
        poly::span<const std::vector<float>> fitness,
        poly::span<const float> crowdingDistance,
        size_t numCandidates)
{
    return CrowdingDistanceSelector{}.selectLeastCrowded(
            fitness, crowdingDistance, numCandidates);
}

} // namespace openzl::training
