// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <vector>
#include "openzl/cpp/poly/Span.hpp"

namespace openzl::training {

/**
 * The algorithm works by sorting the candidates by crowding distance and
 * greedily removing the candidates with the lowest crowding distance then
 * recalculating crowding distance until @param numCandidates is reached.
 * Since removing a point only affects the crowding distance of neighbors in
 * each dimension, the algorithm updates these crowding distances after each
 * removal. This allows it to run in O(lg n) time per removal.
 *
 * @returns a vector of indices that maximizes the minimum crowding distance
 * of any point.
 * @param fitness is a vector of fitness values for each candidate.
 * @param crowdingDistance is a vector of crowding distances for each
 * candidate.
 */
std::vector<size_t> selectLeastCrowded(
        poly::span<const std::vector<float>> fitness,
        poly::span<const float> crowdingDistance,
        size_t numCandidates);

} // namespace openzl::training
