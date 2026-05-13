// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openzl/common/allocation.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/shared/a1cbor.h"
#include "openzl/zl_localParams.h"

namespace openzl::training::graph_mutation {

/**
 * @brief Checks if a compressor contains a graph with a specific prefix.
 */
bool hasTargetGraph(
        const Compressor& compressor,
        poly::string_view targetGraphPrefix);

/**
 * @brief Extracts the base name of a graph by splitting at '#'
 * character.
 *
 * @param graphName The full graph name
 * @return The base name (prefix before '#')
 */
std::string_view getGraphBasePrefix(std::string_view graphName);

/**
 * @brief Finds all graphs with a specific prefix in a serialized compressor.
 *
 * This function decodes the serialized compressor into a CBOR structure and
 * searches for all graphs whose base prefix (determined by getGraphBasePrefix)
 * matches the given prefix. A serialized compressor is a CBOR-encoded data
 * structure containing compression graph definitions.
 *
 * @param compressor The compressor to search.
 * @param prefix The prefix to search for in graph names.
 * @return std::vector<std::string> A vector of graph names that match the
 * prefix.
 */
std::vector<GraphID> findAllGraphsWithPrefix(
        const Compressor& compressor,
        poly::string_view prefix);

/// @returns the custom graphs of @p graphid
std::vector<GraphID> getCustomGraphs(
        const Compressor& compressor,
        GraphID graph);

/// @returns The custom nodes of @p graphid
std::vector<NodeID> getCustomNodes(const Compressor& compressor, GraphID graph);

} // namespace openzl::training::graph_mutation
