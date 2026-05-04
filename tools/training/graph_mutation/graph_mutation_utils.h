// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "openzl/common/allocation.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/shared/a1cbor.h"
#include "openzl/zl_localParams.h"

namespace openzl::training::graph_mutation {

/**
 * @brief Creates a shared_ptr to a string_view from a string.
 *
 * This utility function creates a shared_ptr to a string_view that references
 * the provided string. The string is stored in a bundle object that is managed
 * by the shared_ptr, ensuring that the string_view remains valid as long as
 * the shared_ptr exists.
 *
 * @param str The string to create a string_view from.
 * @return std::shared_ptr<const std::string_view> A shared_ptr to a string_view
 * that references the provided string.
 */
std::shared_ptr<const std::string_view> createSharedStringView(std::string str);

/**
 * @brief Checks if a compressor contains a graph with a specific prefix.
 */
bool hasTargetGraph(
        const Compressor& compressor,
        poly::string_view targetGraphPrefix);

/**
 * @brief Replaces the base graph of a specific parameterized graph.
 *
 * This function updates the "base" field of a specific parameterized graph
 * to point to a new base graph. Unlike replaceGraphInCompressor2, this only
 * modifies the base field of the specified graph and does not update other
 * references throughout the compressor. It also clears the other fields of
 * the parameterized graph.
 *
 * @param serializedCompressor The serialized compressor to modify.
 * @param parameterizedGraphName The name of the parameterized graph whose
 * base should be updated.
 * @param newBaseGraphName The name of the new base graph to reference.
 * @return The serialized modified compressor as CBOR data.
 */
std::string replaceBaseGraphInCompressor(
        const std::string_view serializedCompressor,
        const std::string& parameterizedGraphName,
        const std::string& newBaseGraphName);

/**
 * @brief Extracts the base name of a graph by splitting at '#'
 * character.
 *
 * @param graphName The full graph name
 * @return The base name (prefix before '#')
 */
std::string_view getGraphBasePrefix(std::string_view graphName);

/**
 * @brief Decodes a serialized compressor into a CBOR structure.
 *
 * @param serialized The serialized compressor to decode.
 * @return A tuple containing the root CBOR item and the arena used to decode
 */
std::tuple<std::shared_ptr<const A1C_Item>, std::shared_ptr<Arena>>
decodeSerializedCompressorIntoCbor(const std::string_view serialized);

/**
 * @brief Encodes a CBOR item into serialized binary data.
 *
 * @param root The CBOR item to encode
 * @return std::shared_ptr<const std::string_view> A shared pointer to a string
 * view containing the serialized binary data
 * @throws std::runtime_error If encoding fails
 */
std::shared_ptr<const std::string_view> encodeCborAsSerialized(
        const A1C_Item* root);

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
