// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <utility>
#include <vector>

#include "openzl/codecs/zl_lz.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/LocalParams.hpp"
#include "openzl/cpp/codecs/Graph.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"
#include "openzl/cpp/codecs/Node.hpp"

namespace openzl {
namespace detail {
template <typename Parameters>
void addLzLocalParams(LocalParams& lp, const Parameters& params)
{
    if (params.compressionLevel.has_value()) {
        lp.addIntParam(
                ZL_LzParam_compressionLevel, params.compressionLevel.value());
    }
    if (params.acceleration.has_value()) {
        lp.addIntParam(ZL_LzParam_acceleration, params.acceleration.value());
    }
    if (params.windowLog.has_value()) {
        lp.addIntParam(ZL_LzParam_windowLog, params.windowLog.value());
    }
}
} // namespace detail

namespace nodes {
struct Lz : public Node {
   public:
    static constexpr NodeID node = ZL_NODE_LZ;

    static constexpr NodeMetadata<1, 4> metadata = {
        .inputs           = { InputMetadata{ .type = Type::Serial } },
        .singletonOutputs = { OutputMetadata{ .type = Type::Serial,
                                              .name = "literals" },
                              OutputMetadata{ .type = Type::Numeric,
                                              .name = "offsets" },
                              OutputMetadata{ .type = Type::Numeric,
                                              .name = "literal lengths" },
                              OutputMetadata{ .type = Type::Numeric,
                                              .name = "match lengths" } },
        .description      = "LZ77 compress a serial byte stream",
    };

    struct Parameters {
        /// Optionally override the compression level
        poly::optional<int> compressionLevel;
        /// Optionally override the match finder acceleration
        poly::optional<int> acceleration;
        /// Optionally override the maximum lookback window log
        poly::optional<int> windowLog;
    };

    Lz() {}
    explicit Lz(Parameters p) : params_(std::move(p)) {}
    explicit Lz(int compressionLevel)
            : Lz(Parameters{ .compressionLevel = compressionLevel })
    {
    }

    NodeID baseNode() const override
    {
        return node;
    }

    poly::optional<NodeParameters> parameters() const override
    {
        if (!params_.has_value()) {
            return poly::nullopt;
        }

        LocalParams lp;
        ::openzl::detail::addLzLocalParams(lp, *params_);
        return NodeParameters{ .name        = "lz_with_parameters",
                               .localParams = std::move(lp) };
    }

    /**
     * Helper method to build a graph, since this is the most common operation,
     * and the one that benefits most from brevity, since it is often nested.
     */
    GraphID operator()(
            Compressor& compressor,
            GraphID literals,
            GraphID offsets,
            GraphID literalLengths,
            GraphID matchLengths) const
    {
        return buildGraph(
                compressor,
                std::initializer_list<GraphID>{
                        literals, offsets, literalLengths, matchLengths });
    }

    ~Lz() override = default;

   private:
    poly::optional<Parameters> params_{};
};
} // namespace nodes

namespace graphs {

class Lz : public Graph {
   public:
    static constexpr GraphID graph = ZL_GRAPH_LZ;

    static constexpr GraphMetadata<1> metadata = {
        .inputs      = { InputMetadata{ .typeMask = TypeMask::Serial } },
        .description = "LZ77 compress a serial byte stream",
    };

    using NodeParams = nodes::Lz::Parameters;

    struct Parameters {
        /// Optionally override the node parameters
        NodeParams nodeParams;
        /// Optionally override the backend literals graph
        poly::optional<GraphID> literalsGraph;
        /// Optionally override the backend offsets graph
        poly::optional<GraphID> offsetsGraph;
        /// Optionally override the backend muxed bytes graph
        poly::optional<GraphID> muxedBytesGraph;
        /// Optionally override the backend overflow lengths graph
        poly::optional<GraphID> overflowLengthsGraph;
        /// Optionally override the backend mux lengths graph
        poly::optional<GraphID> muxLengthsGraph;
    };

    Lz() {}
    explicit Lz(Parameters p) : params_(std::move(p)) {}
    explicit Lz(NodeParams p) : Lz(Parameters{ .nodeParams = std::move(p) }) {}
    explicit Lz(int compressionLevel)
            : Lz(NodeParams{ .compressionLevel = compressionLevel })
    {
    }

    GraphID baseGraph() const override
    {
        return graph;
    }

    poly::optional<GraphParameters> parameters() const override
    {
        if (!params_.has_value()) {
            return poly::nullopt;
        }

        LocalParams lp;
        ::openzl::detail::addLzLocalParams(lp, params_->nodeParams);

        std::vector<GraphID> graphs;
        auto addGraph = [&](int key, poly::optional<GraphID> g) {
            if (g.has_value()) {
                lp.addIntParam(key, int(graphs.size()));
                graphs.push_back(g.value());
            }
        };
        addGraph(ZL_LzParam_literalsGraphIdx, params_->literalsGraph);
        addGraph(ZL_LzParam_offsetsGraphIdx, params_->offsetsGraph);
        addGraph(ZL_LzParam_muxedBytesGraphIdx, params_->muxedBytesGraph);
        addGraph(
                ZL_LzParam_overflowLengthsGraphIdx,
                params_->overflowLengthsGraph);
        addGraph(ZL_LzParam_muxLengthsGraphIdx, params_->muxLengthsGraph);

        return GraphParameters{
            .customGraphs = std::move(graphs),
            .localParams  = std::move(lp),
        };
    }

    ~Lz() override = default;

   private:
    poly::optional<Parameters> params_{};
};
} // namespace graphs
} // namespace openzl
