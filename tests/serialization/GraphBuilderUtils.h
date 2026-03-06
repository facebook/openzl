// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>

#include "openzl/cpp/Compressor.hpp"
#include "tests/datagen/DataGen.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests {

/**
 * Calculates the compressed bound for the given input, including for multi
 * inputs.
 *
 * @param input The input to compress.
 *
 * @return The compressed bound for the given input.
 */
size_t getCompressedBound(const std::unique_ptr<OpenZLInput>& input);

/**
 * Partitions a string into random segments and returns the lengths of each
 * segment. This is done by numbering a number of segments, and selecting the
 * desired number without replacement.
 *
 * @param input The string to partition. Will be shuffled in place.
 * @param gen Random number generator for determining segment count and
 * shuffling.
 * @return Vector of segment lengths that sum to input.size(), or empty if
 *         input is empty.
 */
std::vector<uint32_t> getPartitionedStringLengths(
        const std::string& input,
        int numSegments,
        datagen::DataGen& gen);

/**
 * Generates a random input that is compatible with the given graph's input
 * requirements. Inspects the graph's input mask to determine valid input types
 * and generates random data of a compatible type.
 *
 * @param compressor The compressor containing the graph.
 * @param gen Random number generator for input generation.
 * @param startingGraph The graph ID to generate compatible input for.
 * @return A unique pointer to an OpenZLInput compatible with the graph.
 */
std::unique_ptr<OpenZLInput> generateInputCompatibleWithGraph(
        Compressor& compressor,
        datagen::DataGen& gen,
        ZL_GraphID startingGraph);

} // namespace openzl::tests
