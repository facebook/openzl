// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>
#include <memory>
#include <string>

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/allocation.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/zl_reflection.h"

#include "tools/logger/Logger.h"
#include "tools/training/graph_mutation/graph_mutation_utils.h"

namespace openzl::training::graph_mutation {

using namespace tools::logger;

namespace {

template <typename Fn>
std::vector<GraphID> findGraphsWhere(const Compressor& compressor, Fn&& fn)
{
    struct State {
        Fn& fn;
        std::vector<GraphID> result{};
    };
    State state{ fn };

    auto callback = [](void* opaque,
                       const ZL_Compressor* c,
                       ZL_GraphID graph) noexcept {
        const CompressorRef cref(const_cast<ZL_Compressor*>(c));
        auto& s = *static_cast<State*>(opaque);
        try {
            if (s.fn(cref, graph)) {
                s.result.push_back(graph);
            }
        } catch (...) {
            return ZL_returnError(ZL_ErrorCode_GENERIC);
        }
        return ZL_returnSuccess();
    };

    compressor.unwrap(
            ZL_Compressor_forEachGraph(compressor.get(), callback, &state));
    return state.result;
}

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
    return !findAllGraphsWithPrefix(compressor, targetGraphPrefix).empty();
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

std::vector<GraphID> findAllGraphsWithPrefix(
        const Compressor& compressor,
        poly::string_view prefix)
{
    return findGraphsWhere(
            compressor, [&prefix](const Compressor& c, GraphID g) {
                auto graphName = ZL_Compressor_Graph_getName(c.get(), g);
                return getGraphBasePrefix(graphName) == prefix;
            });
}

std::vector<GraphID> getCustomGraphs(
        const Compressor& compressor,
        GraphID graph)
{
    auto graphs = ZL_Compressor_Graph_getCustomGraphs(compressor.get(), graph);
    return { graphs.graphids, graphs.graphids + graphs.nbGraphIDs };
}

/// @returns The custom nodes of @p graphid
std::vector<NodeID> getCustomNodes(const Compressor& compressor, GraphID graph)
{
    auto nodes = ZL_Compressor_Graph_getCustomNodes(compressor.get(), graph);
    return { nodes.nodeids, nodes.nodeids + nodes.nbNodeIDs };
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

} // namespace openzl::training::graph_mutation
