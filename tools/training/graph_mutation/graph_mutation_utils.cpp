// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include <memory>
#include <string>

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/allocation.h"
#include "openzl/cpp/Compressor.hpp"

#include "tools/logger/Logger.h"
#include "tools/training/graph_mutation/graph_mutation_utils.h"

namespace openzl::training::graph_mutation {

using namespace tools::logger;

namespace {

/*
 * @brief A wrapper class for a CBOR data bundle.
 */
struct CborDataBundle {
    std::string buffer;
    std::string_view view;

    explicit CborDataBundle(std::string buf)
            : buffer(std::move(buf)), view(buffer)
    {
    }

    static std::shared_ptr<const std::string_view> create(std::string buffer)
    {
        auto bundle = std::make_shared<CborDataBundle>(std::move(buffer));
        return std::shared_ptr<const std::string_view>(bundle, &bundle->view);
    }
};

/**
 * @brief Gets the start graph name from the root item.
 *
 * This function extracts the name of the starting graph from the root CBOR
 * item. It checks for a valid "start" field in the root map and returns its
 * value.
 * @param root The root CBOR item containing the start graph name
 * @return std::string The name of the start graph
 * @throws std::runtime_error If the start graph name cannot be found
 */
std::string getStartGraphName(const A1C_Item* root)
{
    A1C_Item* startField = A1C_Map_get_cstr(&root->map, "start");
    if (!startField || startField->type != A1C_ItemType_string) {
        throw Exception("No valid start key found in the graph");
    }

    return std::string(startField->string.data, startField->string.size);
}

/**
 * @brief Replaces all references to a graph name in other graphs' dependency
 * arrays.
 *
 * Scans through all graphs in the compressor and updates any references to the
 * old graph name found in their "graphs" arrays (dependency lists). This
 * function only updates references - it does not modify the original graph
 * definition key, the start graph field, or base graph references.
 *
 * @param graphsItem The CBOR item containing all graphs map
 * @param oldName The old graph name to find and replace in references
 * @param newName The new graph name to substitute in references
 * @param arena The memory arena for CBOR string operations
 */
void replaceGraphNameReferences(
        A1C_Item* graphsItem,
        std::string_view oldName,
        std::string_view newName,
        A1C_Arena* arena)
{
    // Update all references to the old graph name in all graphs
    for (size_t i = 0; i < graphsItem->map.size; ++i) {
        A1C_Pair* pair = &graphsItem->map.items[i];
        if (pair->val.type == A1C_ItemType_map) {
            // Update references in graphs arrays
            A1C_Item* graphsArray = A1C_Map_get_cstr(&pair->val.map, "graphs");
            if (graphsArray && graphsArray->type == A1C_ItemType_array) {
                for (size_t j = 0; j < graphsArray->array.size; ++j) {
                    A1C_Item* graphRef = A1C_Array_get(&graphsArray->array, j);
                    if (graphRef && graphRef->type == A1C_ItemType_string) {
                        StringView refView =
                                StringView_initFromA1C(graphRef->string);
                        std::string_view refName(refView.data, refView.size);
                        if (refName == oldName) {
                            if (!A1C_Item_string_copy(
                                        graphRef,
                                        newName.data(),
                                        newName.length(),
                                        arena)) {
                                throw Exception(
                                        "Failed to update graph reference");
                            }
                            Logger::log_c(
                                    VERBOSE2,
                                    "Updated graph reference from %.*s to %.*s in graph %.*s",
                                    (int)oldName.size(),
                                    oldName.data(),
                                    (int)newName.size(),
                                    newName.data(),
                                    (int)pair->key.string.size,
                                    pair->key.string.data);
                        }
                    }
                }
            }
        }
    }
}

enum class GraphFindStrategy {
    Exact, // Find by exact name match
    Prefix // Find all graphs with matching prefix
};

struct GraphFindResult {
    A1C_Pair* pair = nullptr;       // For exact name searches
    std::vector<std::string> names; // For all-with-prefix searches

    explicit operator bool() const
    {
        return pair != nullptr || !names.empty();
    }
};

/**
 * @brief Find graphs (exact name or prefix) in the graphs map.
 *
 * @param graphsItem The graphs map item (must be valid)
 * @param searchTerm The name or prefix to search for
 * @param strategy The search strategy to use
 * @return GraphFindResult containing the results based on strategy
 */
GraphFindResult findGraphInMap(
        A1C_Item* graphsItem,
        std::string_view searchTerm,
        GraphFindStrategy strategy)
{
    GraphFindResult result;

    for (size_t i = 0; i < graphsItem->map.size; ++i) {
        A1C_Pair* pair = &graphsItem->map.items[i];
        if (pair->key.type != A1C_ItemType_string) {
            continue;
        }

        StringView keyView = StringView_initFromA1C(pair->key.string);
        std::string_view keySv(keyView.data, keyView.size);

        bool matches = false;
        switch (strategy) {
            case GraphFindStrategy::Exact:
                matches = (keySv == searchTerm);
                break;
            case GraphFindStrategy::Prefix:
                matches =
                        (getGraphBasePrefix(keySv) == std::string(searchTerm));
                break;
        }

        if (matches) {
            if (pair->val.type != A1C_ItemType_map) {
                if (strategy == GraphFindStrategy::Exact) {
                    throw Exception(
                            "Graph '" + std::string(searchTerm)
                            + "' is not a valid map");
                }
                continue; // Skip invalid graphs for prefix searches
            }

            switch (strategy) {
                case GraphFindStrategy::Exact:
                    Logger::log_c(
                            VERBOSE2,
                            "Found target graph: %.*s",
                            (int)keySv.size(),
                            keySv.data());
                    result.pair = pair;
                    return result; // Return immediately for exact match

                case GraphFindStrategy::Prefix:
                    result.names.emplace_back(keySv);
                    break; // Continue searching for all matches
            }
        }
    }

    return result;
}

A1C_Item* extractGraphsFromCbor(std::shared_ptr<const A1C_Item> root)
{
    A1C_Item* graphsItem = A1C_Map_get_cstr(&root->map, "graphs");
    if (!graphsItem || graphsItem->type != A1C_ItemType_map) {
        throw Exception("Could not find valid 'graphs' map in root");
    }

    return graphsItem;
}

const A1C_Item* findGraphByPrefix(
        const std::string_view& serializedCompressor,
        std::string_view targetGraphPrefix)
{
    auto [root, arena] =
            decodeSerializedCompressorIntoCbor(serializedCompressor);
    auto graphsItem = extractGraphsFromCbor(root);

    auto result = findGraphInMap(
            graphsItem, targetGraphPrefix, GraphFindStrategy::Prefix);

    if (result.names.empty()) {
        return nullptr;
    }

    auto exactResult = findGraphInMap(
            graphsItem, result.names[0], GraphFindStrategy::Exact);

    return exactResult.pair ? &exactResult.pair->val : nullptr;
}

} // anonymous namespace

std::shared_ptr<const std::string_view> encodeCborAsSerialized(
        const A1C_Item* root)
{
    size_t cborSize = A1C_Item_encodedSize(root);
    if (cborSize == 0) {
        throw Exception("Failed to determine CBOR size");
    }

    std::string cborBuffer;
    cborBuffer.resize(cborSize);

    A1C_Error error;
    size_t bytesWritten = A1C_Item_encode(
            root,
            reinterpret_cast<uint8_t*>(cborBuffer.data()),
            cborSize,
            &error);
    if (bytesWritten == 0) {
        throw Exception(
                "Failed to encode CBOR: "
                + std::string(A1C_ErrorType_getString(error.type)));
    }

    cborBuffer.resize(bytesWritten);

    return CborDataBundle::create(std::move(cborBuffer));
}

/**
 * @brief Extracts the base name of a graph by splitting at '#' character.
 *
 * @param graphName The full graph name
 * @return The base name (prefix before '#')
 */
std::string_view getGraphBasePrefix(std::string_view graphName)
{
    size_t hashPos = graphName.find('#');
    if (hashPos != std::string_view::npos) {
        return graphName.substr(0, hashPos);
    }
    return graphName;
}

std::tuple<std::shared_ptr<const A1C_Item>, std::shared_ptr<Arena>>
decodeSerializedCompressorIntoCbor(const std::string_view serialized)
{
    auto arena = std::shared_ptr<Arena>(
            ALLOC_HeapArena_create(), ALLOC_Arena_freeArena);
    const A1C_Arena a1cArena = A1C_Arena_wrap(arena.get());

    A1C_Decoder decoder;
    const A1C_DecoderConfig config = { .maxDepth            = 0,
                                       .limitBytes          = 0,
                                       .referenceSource     = true,
                                       .rejectUnknownSimple = true };
    A1C_Decoder_init(&decoder, a1cArena, config);

    const A1C_Item* root = A1C_Decoder_decode(
            &decoder,
            reinterpret_cast<const uint8_t*>(serialized.data()),
            serialized.size());

    if (!root) {
        throw Exception("Failed to parse CBOR data");
    }

    if (root->type != A1C_ItemType_map) {
        throw Exception("Root is not a map");
    }

    auto rootPtr =
            std::shared_ptr<const A1C_Item>(root, [arena](const A1C_Item*) {
                // The arena will be destroyed when this deleter is called
            });

    return std::make_tuple(rootPtr, arena);
}

bool hasTargetGraph(
        const Compressor& compressor,
        poly::string_view targetGraphPrefix)
{
    Logger::log_c(
            VERBOSE2,
            "In hasTargetGraph. targetGraphPrefix: %.*s",
            (int)targetGraphPrefix.size(),
            targetGraphPrefix.data());
    const std::string serializedData = compressor.serialize();
    return findGraphByPrefix(serializedData, targetGraphPrefix) != nullptr;
}

std::vector<std::string> extractSuccessorsFromCbor(
        const std::string_view& cborStr,
        const std::string& graphName)
{
    // Decode once and keep the arena alive
    auto [root, arena] = decodeSerializedCompressorIntoCbor(cborStr);
    auto graphsItem    = extractGraphsFromCbor(root);

    // Find the exact graph using the same decoded data
    auto exactResult =
            findGraphInMap(graphsItem, graphName, GraphFindStrategy::Exact);

    if (!exactResult.pair) {
        throw Exception("Could not find exact graph named '" + graphName + "'");
    }

    const A1C_Item* targetGraph = &exactResult.pair->val;

    // Get the "graphs" array from the target graph (contains successor graph
    // names)
    A1C_Item const* const graphsArray =
            A1C_Map_get_cstr(&targetGraph->map, "graphs");
    if (!graphsArray || graphsArray->type != A1C_ItemType_array) {
        throw Exception(
                "'" + graphName
                + "' does not contain 'graphs' key or it's not an array");
    }

    std::vector<std::string> successorsFromSerialization;
    for (size_t i = 0; i < graphsArray->array.size; ++i) {
        A1C_Item const* const item = A1C_Array_get(&graphsArray->array, i);
        if (item && item->type == A1C_ItemType_string) {
            StringView successorView = StringView_initFromA1C(item->string);
            successorsFromSerialization.emplace_back(
                    successorView.data, successorView.size);
        }
    }
    return successorsFromSerialization;
}

std::vector<std::string> extractNodesFromCbor(
        const std::string_view& cborStr,
        const std::string& targetGraphPrefix)
{
    // Decode once and keep the arena alive
    auto [root, arena] = decodeSerializedCompressorIntoCbor(cborStr);
    auto graphsItem    = extractGraphsFromCbor(root);

    // Find all graphs with the prefix
    auto result = findGraphInMap(
            graphsItem, targetGraphPrefix, GraphFindStrategy::Prefix);

    if (result.names.empty()) {
        throw Exception(
                "JSON does not contain any graph with name starting with '"
                + targetGraphPrefix + "'");
    }

    // Find the exact graph using the same decoded data
    auto exactResult = findGraphInMap(
            graphsItem, result.names[0], GraphFindStrategy::Exact);

    if (!exactResult.pair) {
        throw Exception(
                "Could not find exact graph for prefix '" + targetGraphPrefix
                + "'");
    }

    const A1C_Item* targetGraph = &exactResult.pair->val;

    // Get the "nodes" array from the target graph
    A1C_Item const* const nodesArray =
            A1C_Map_get_cstr(&targetGraph->map, "nodes");
    if (!nodesArray || nodesArray->type != A1C_ItemType_array) {
        throw Exception(
                "'" + targetGraphPrefix
                + "' does not contain 'nodes' key or it's not an array");
    }

    std::vector<std::string> nodesFromSerialization;
    for (size_t i = 0; i < nodesArray->array.size; ++i) {
        A1C_Item const* const item = A1C_Array_get(&nodesArray->array, i);
        if (item && item->type == A1C_ItemType_string) {
            StringView nodeView = StringView_initFromA1C(item->string);
            nodesFromSerialization.emplace_back(nodeView.data, nodeView.size);
        }
    }
    return nodesFromSerialization;
}

std::shared_ptr<const std::string_view> createSharedStringView(std::string str)
{
    return CborDataBundle::create(std::move(str));
}

std::vector<std::string> findAllGraphsWithPrefix(
        std::string_view serializedCompressor,
        const std::string& prefix)
{
    auto [root, arena] =
            decodeSerializedCompressorIntoCbor(serializedCompressor);
    auto graphsItem = extractGraphsFromCbor(root);

    auto result = findGraphInMap(graphsItem, prefix, GraphFindStrategy::Prefix);
    return result.names;
}

std::string renameGraphInCompressor(
        const std::string_view serializedCompressor,
        std::string_view oldGraphName,
        std::string_view newGraphName)
{
    auto [root, arena] =
            decodeSerializedCompressorIntoCbor(serializedCompressor);
    auto graphsItem = extractGraphsFromCbor(root);

    // Verify the old graph exists
    auto targetGraph =
            findGraphInMap(graphsItem, oldGraphName, GraphFindStrategy::Exact);
    if (!targetGraph.pair) {
        throw Exception(
                "Could not find target graph '" + std::string(oldGraphName)
                + "' in the graphs map");
    }

    A1C_Arena a1cArena = A1C_Arena_wrap(arena.get());
    replaceGraphNameReferences(
            graphsItem, oldGraphName, newGraphName, &a1cArena);

    // Update the "start" field if it points to the old graph name
    A1C_Item* startField = A1C_Map_get_cstr(&root->map, "start");
    if (startField && startField->type == A1C_ItemType_string) {
        StringView currentStartView =
                StringView_initFromA1C(startField->string);
        std::string_view currentStart(
                currentStartView.data, currentStartView.size);
        if (currentStart == oldGraphName) {
            if (!A1C_Item_string_copy(
                        startField,
                        newGraphName.data(),
                        newGraphName.length(),
                        &a1cArena)) {
                throw Exception("Failed to update start field");
            }
            Logger::log_c(
                    VERBOSE2,
                    "Updated start field from '%.*s' to '%.*s'",
                    (int)oldGraphName.size(),
                    oldGraphName.data(),
                    (int)newGraphName.size(),
                    newGraphName.data());
        }
    }

    auto serializedResult = encodeCborAsSerialized(root.get());
    return std::string(*serializedResult);
}

std::string replaceBaseGraphInCompressor(
        const std::string_view serializedCompressor,
        const std::string& parameterizedGraphName,
        const std::string& newBaseGraphName)
{
    auto [root, arena] =
            decodeSerializedCompressorIntoCbor(serializedCompressor);
    auto graphsItem = extractGraphsFromCbor(root);

    // Find the parameterized graph by exact name
    auto targetGraph = findGraphInMap(
            graphsItem, parameterizedGraphName, GraphFindStrategy::Exact);
    if (!targetGraph.pair) {
        throw Exception(
                "Could not find parameterized graph '" + parameterizedGraphName
                + "' in the graphs map");
    }

    if (targetGraph.pair->val.type != A1C_ItemType_map) {
        throw Exception("Invalid parameterized graph");
    }
    auto type = A1C_Map_get_cstr(&targetGraph.pair->val.map, "type");
    if (!type || type->type != A1C_ItemType_string) {
        throw Exception("Invalid parameterized graph");
    }
    if (poly::string_view(type->string.data, type->string.size)
        != "parameterized") {
        throw Exception("Invalid parameterized graph");
    }

    A1C_Arena a1cArena = A1C_Arena_wrap(arena.get());
    A1C_Pair* map      = A1C_Item_map(&targetGraph.pair->val, 4, &a1cArena);
    if (map == nullptr) {
        throw std::bad_alloc();
    }
    A1C_Item_string_refCStr(&map[0].key, "type");
    A1C_Item_string_refCStr(&map[0].val, "parameterized");
    A1C_Item_string_refCStr(&map[1].key, "base");
    if (!A1C_Item_string_cstr(
                &map[1].val, newBaseGraphName.c_str(), &a1cArena)) {
        throw std::bad_alloc();
    }
    A1C_Item_string_refCStr(&map[2].key, "graphs");
    if (A1C_Item_array(&map[2].val, 0, &a1cArena) == nullptr) {
        throw std::bad_alloc();
    }
    A1C_Item_string_refCStr(&map[3].key, "nodes");
    if (A1C_Item_array(&map[3].val, 0, &a1cArena) == nullptr) {
        throw std::bad_alloc();
    }

    Logger::log_c(
            VERBOSE2,
            "Updated base field of graph '%s' to '%s'",
            parameterizedGraphName.c_str(),
            newBaseGraphName.c_str());

    auto serializedResult = encodeCborAsSerialized(root.get());
    return std::string(*serializedResult);
}

/**
 * @brief Gets the maximum ID from all graphs with '#' in the name.
 *
 * Serialized graph definions are stored in the format {name}#{id}, where
 * {name} is the graph name and {id} is a positive integer. The IDs are unique
 * across all graphs of all names. {name} can be empty.

 * This function iterates through all graphs, extracts the suffix after the
 * '#' and returns the maximum value found. Graphs without '#' are skipped.
 *
 * @param serializedCompressor The serialized compressor containing the graphs
 * @return int The maximum ID found, or 0 if no graphs with '#' exist
 */
int getMaximumIdFromSerialized(std::string_view serializedCompressor)
{
    auto [root, arena] =
            decodeSerializedCompressorIntoCbor(serializedCompressor);
    auto graphsItem = extractGraphsFromCbor(root);

    auto extractId = [](const A1C_Pair& pair) -> int {
        if (pair.key.type != A1C_ItemType_string) {
            return 0;
        }

        std::string graphName(pair.key.string.data, pair.key.string.size);
        size_t hashPos = graphName.find('#');
        if (hashPos != std::string::npos && hashPos + 1 < graphName.length()) {
            std::string suffix = graphName.substr(hashPos + 1);
            try {
                return std::stoi(suffix);
            } catch (const std::exception&) {
                // Skip invalid suffixes
                return 0;
            }
        }
        return 0;
    };

    A1C_Pair* items = graphsItem->map.items;
    size_t size     = graphsItem->map.size;
    auto maxElement = std::max_element(
            items,
            items + size,
            [&extractId](const A1C_Pair& a, const A1C_Pair& b) {
                return extractId(a) < extractId(b);
            });
    return (maxElement != items + size) ? extractId(*maxElement) : 0;
}

} // namespace openzl::training::graph_mutation
