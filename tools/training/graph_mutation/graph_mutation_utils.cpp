// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <memory>
#include <string>

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

/**
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

} // anonymous namespace

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

} // namespace openzl::training::graph_mutation
