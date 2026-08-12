// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "openzl/cpp/poly/Span.hpp"
#include "tools/training/ace/ace_compressor.h"

namespace openzl {
namespace training {
constexpr size_t kDefaultMaxDepth = 10;

/// @returns the universe of all possible nodes that ACE can use to build its
/// compressors
poly::span<const ACENode> getAllNodes();

/// @returns the universe of all possible graphs that ACE can use to build its
/// compressors
poly::span<const ACEGraph> getAllGraphs();

/// @returns the subset of `getAllNodes()` that are compatible with the given
/// @p inputType and usable at @p formatVersion (nodes whose minFormatVersion is
/// 0 are treated as unconstrained).
std::vector<ACENode> getNodesComptabileWith(
        Type inputType,
        uint32_t formatVersion);
/// @returns the subset of `getAllGraphs()` that are compatible with the given
/// @p inputType
poly::span<const ACEGraph> getGraphsComptabileWith(Type inputType);

/// @returns The set of pre-built compressors that are compatible with the given
/// @p inputType. These are hand crafted compressors that can work well for
/// inputs of the given type. These are used to seed the initial population of
/// ACE.
poly::span<const ACECompressor> getPrebuiltCompressors(Type inputType);

/// @returns A random compressor that is compatible with the given @p inputType
/// that is a single graph.
ACECompressor buildRandomGraphCompressor(std::mt19937_64& rng, Type inputType);

/// @returns A random compressor that is compatible with the given @p inputType
/// that is a single node followed by ACECompressor successors. Only nodes
/// usable at @p formatVersion are considered.
ACECompressor buildRandomNodeCompressor(
        std::mt19937_64& rng,
        Type inputType,
        uint32_t formatVersion,
        size_t maxDepth = kDefaultMaxDepth);

/// @returns A random compressor that is compatible with the given @p inputType.
/// Only nodes usable at @p formatVersion are considered.
ACECompressor buildRandomCompressor(
        std::mt19937_64& rng,
        Type inputType,
        uint32_t formatVersion,
        size_t maxDepth = kDefaultMaxDepth);

ACECompressor buildStoreCompressor();

ACECompressor buildCompressGenericCompressor();

} // namespace training
} // namespace openzl
