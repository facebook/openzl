// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>
#include <vector>

#include "openzl/cpp/CParam.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/Input.hpp"
#include "openzl/cpp/LocalParams.hpp"
#include "openzl/cpp/Type.hpp"
#include "openzl/cpp/poly/Optional.hpp"
#include "openzl/cpp/poly/Span.hpp"
#include "openzl/zl_graph_api.h"

namespace openzl {
using GraphPerformance = ZL_GraphPerformance;

class Edge {
   public:
    explicit Edge(ZL_Edge* edge);

    using RunNodeResult = std::vector<Edge>;

    /// Safely converts a span of ZL_Edge* into a span of Edge.
    static RunNodeResult convert(poly::span<ZL_Edge*> edges);
    static RunNodeResult convert(ZL_EdgeList edges)
    {
        return Edge::convert(
                poly::span<ZL_Edge*>{ edges.edges, edges.nbEdges });
    }
    static std::vector<ZL_Edge*> convert(poly::span<Edge> edges);

    ZL_Edge* get()
    {
        return edge_;
    }
    const ZL_Edge* get() const
    {
        return edge_;
    }

    const InputRef& getInput() const
    {
        return input_;
    }

    RunNodeResult runNode(
            NodeID node,
            const poly::optional<NodeParameters>& params = poly::nullopt);

    static RunNodeResult runMultiInputNode(
            poly::span<Edge> inputs,
            NodeID node,
            const poly::optional<NodeParameters>& params = poly::nullopt);

    void setIntMetadata(int key, int value);
    void setDestination(
            GraphID graph,
            const poly::optional<GraphParameters>& params = poly::nullopt);
    static void setMultiInputDestination(
            poly::span<Edge> inputs,
            GraphID graph,
            const poly::optional<GraphParameters>& params = poly::nullopt);

   private:
    ZL_Edge* edge_;
    InputRef input_; // Must only provide const access
};

class GraphState {
   public:
    explicit GraphState(ZL_Graph* graph, poly::span<ZL_Edge*> edges)
            : graph_(graph), edges_(Edge::convert(edges))
    {
    }

    ZL_Graph* get()
    {
        return graph_;
    }

    const ZL_Graph* get() const
    {
        return graph_;
    }

    poly::span<Edge> edges()
    {
        return edges_;
    }
    poly::span<const Edge> edges() const
    {
        return edges_;
    }

    poly::span<const GraphID> customGraphs() const;

    poly::span<const NodeID> customNodes() const;

    int getCParam(CParam param) const;
    poly::optional<int> getLocalIntParam(int key) const;
    poly::optional<poly::span<const uint8_t>> getLocalParam(int key) const;

    void* getScratchSpace(size_t size);

    bool isNodeSupported(NodeID node) const;

    poly::optional<GraphPerformance> tryGraph(
            const Input& input,
            GraphID graph,
            const poly::optional<GraphParameters>& params =
                    poly::nullopt) const;

    poly::optional<GraphPerformance> tryGraph(
            poly::span<const ZL_Input*> inputs,
            GraphID graph,
            const poly::optional<GraphParameters>& params =
                    poly::nullopt) const;

    poly::string_view getErrorContextString(ZL_Error error) const;

    template <typename ResultType>
    poly::string_view getErrorContextString(ResultType result) const
    {
        return getErrorContextString(ZL_RES_error(result));
    }

    template <typename ResultType>
    typename ResultType::ValueType unwrap(
            ResultType result,
            poly::string_view msg = {},
            poly::source_location location =
                    poly::source_location::current()) const
    {
        return openzl::unwrap(
                result, std::move(msg), this, std::move(location));
    }

   private:
    ZL_Graph* graph_;
    std::vector<Edge> edges_;
};

struct FunctionGraphDescription {
    poly::optional<std::string> name;
    std::vector<TypeMask> inputTypeMasks;
    bool lastInputIsVariable{ false };
    std::vector<GraphID> customGraphs;
    std::vector<NodeID> customNodes;
    poly::optional<LocalParams> localParams;
};

class FunctionGraph {
   public:
    virtual FunctionGraphDescription functionGraphDescription() const = 0;

    virtual void graph(GraphState& state) const = 0;

    virtual ~FunctionGraph() = default;

    static GraphID registerFunctionGraph(
            Compressor& compressor,
            std::shared_ptr<FunctionGraph> functionGraph);
};
} // namespace openzl
