// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/utils/genetic_algorithm.h"

namespace openzl {
namespace training {

std::vector<float> crowdingDistance(
        poly::span<const std::vector<float>> fitness,
        poly::span<const size_t> subset)
{
    if (subset.size() == 0) {
        return {};
    }
    assert(fitness.size() >= subset.size());

    std::vector<float> dist(subset.size(), 0.0);

    std::vector<size_t> indices(subset.size(), 0);
    std::iota(indices.begin(), indices.end(), 0);

    const size_t numDims = fitness[0].size();
    for (size_t dim = 0; dim < numDims; ++dim) {
        auto metric = [&](size_t idx) {
            return std::make_pair(fitness[subset[idx]][dim], idx);
        };

        detail::sortByKey(indices, metric);
        dist[indices.front()] = std::numeric_limits<float>::infinity();
        dist[indices.back()]  = std::numeric_limits<float>::infinity();

        float minMetric = std::numeric_limits<float>::infinity();
        float maxMetric = -std::numeric_limits<float>::infinity();
        for (auto idx : indices) {
            minMetric = std::min(minMetric, metric(idx).first);
            maxMetric = std::max(maxMetric, metric(idx).first);
        }
        const float metricRange = maxMetric - minMetric;
        if (!std::isnormal(metricRange)) {
            // Avoid division by zero or infinity
            continue;
        }
        assert(minMetric <= maxMetric);
        for (size_t i = 1; i < indices.size() - 1; ++i) {
            const auto prevMetric = metric(indices[i - 1]).first;
            const auto nextMetric = metric(indices[i + 1]).first;
            assert(nextMetric >= prevMetric);
            dist[indices[i]] += (nextMetric - prevMetric) / metricRange;
        }
    }
    return dist;
}

bool dominates(poly::span<const float> lhs, poly::span<const float> rhs)
{
    assert(lhs.size() == rhs.size());
    bool strict = false;
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (rhs[i] < lhs[i]) {
            return false;
        }
        if (lhs[i] < rhs[i]) {
            strict = true;
        }
    }
    return strict;
}

std::pair<std::vector<std::vector<size_t>>, std::vector<size_t>>
fastNonDominatedSort(poly::span<const std::vector<float>> fitness)
{
    std::vector<std::vector<size_t>> dominated(fitness.size());
    std::vector<size_t> numDominatedBy(fitness.size(), 0);
    std::vector<size_t> rank(fitness.size(), 0);
    std::vector<std::vector<size_t>> fronts(1);

    for (size_t i = 0; i < fitness.size(); ++i) {
        for (size_t j = 0; j < fitness.size(); ++j) {
            if (dominates(fitness[i], fitness[j])) {
                dominated[i].push_back(j);
            } else if (dominates(fitness[j], fitness[i])) {
                ++numDominatedBy[i];
            }
        }

        if (numDominatedBy[i] == 0) {
            rank[i] = 0;
            fronts[0].push_back(i);
        }
    }

    for (;;) {
        std::vector<size_t> front;
        for (size_t i : fronts.back()) {
            for (size_t j : dominated[i]) {
                assert(numDominatedBy[j] > 0);
                if (--numDominatedBy[j] == 0) {
                    rank[j] = fronts.size();
                    front.push_back(j);
                }
            }
        }
        if (front.size() == 0) {
            break;
        }
        fronts.push_back(std::move(front));
    }

    return { std::move(fronts), std::move(rank) };
}

size_t TournamentSelector::select(
        poly::span<const size_t> rank,
        poly::span<const float> crowdingDistance)
{
    assert(rank.size() == crowdingDistance.size());
    auto candidates = getCandidates(rank.size());
    std::sort(candidates.begin(), candidates.end(), [&](auto a, auto b) {
        return rank[a] < rank[b]
                || (rank[a] == rank[b]
                    && crowdingDistance[a] > crowdingDistance[b]);
    });
    std::bernoulli_distribution dist(params_.torunamentSelectionProbability);
    size_t idx = 0;
    while (idx < candidates.size() - 1 && !dist(gen_)) {
        ++idx;
    }
    return candidates[idx];
}

std::vector<size_t> TournamentSelector::getCandidates(size_t populationSize)
{
    if (populationSize < 1) {
        throw std::logic_error("Population size must be at least 1");
    }
    std::uniform_int_distribution<size_t> dist(0, populationSize - 1);
    std::unordered_set<size_t> candidates;
    const size_t numCandidates =
            std::min(params_.tournamentSize, populationSize);
    while (candidates.size() < numCandidates) {
        candidates.insert(dist(gen_));
    }
    return { candidates.begin(), candidates.end() };
}

} // namespace training
} // namespace openzl
