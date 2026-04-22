// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_mux_lengths.h"
#include "openzl/codecs/zl_store.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"
#include "openzl/cpp/codecs/Node.hpp"

namespace openzl {
namespace nodes {

class MuxLengths : public Node {
   public:
    static constexpr NodeID node = ZL_NODE_MUX_LENGTHS;

    static constexpr NodeMetadata<2, 2> metadata = {
        .inputs           = { InputMetadata{ .type = Type::Numeric,
                                             .name = "literal_lengths" },
                              InputMetadata{ .type = Type::Numeric,
                                             .name = "match_lengths" } },
        .singletonOutputs = { OutputMetadata{ .type = Type::Serial,
                                              .name = "muxed_lengths" },
                              OutputMetadata{ .type = Type::Numeric,
                                              .name = "long_lengths" } },
        .description =
                "Multiplex literal and match lengths into a single byte stream with overflow",
    };

    explicit MuxLengths(
            poly::optional<unsigned> splitPoint      = poly::nullopt,
            poly::optional<unsigned> matchLengthBias = poly::nullopt)
            : splitPoint_(splitPoint), matchLengthBias_(matchLengthBias)
    {
    }

    NodeID baseNode() const override
    {
        return node;
    }

    poly::optional<NodeParameters> parameters() const override
    {
        LocalParams params;
        if (splitPoint_.has_value()) {
            params.addIntParam(ZL_MUX_LENGTHS_SPLIT_POINT_PID, *splitPoint_);
        }
        if (matchLengthBias_.has_value()) {
            params.addIntParam(
                    ZL_MUX_LENGTHS_MATCH_LENGTH_BIAS_PID, *matchLengthBias_);
        }
        return NodeParameters{ .localParams = std::move(params) };
    }

    GraphID operator()(
            Compressor& compressor,
            GraphID muxedLengths,
            GraphID longLengths = ZL_GRAPH_STORE) const
    {
        return buildGraph(
                compressor,
                std::initializer_list<GraphID>{ muxedLengths, longLengths });
    }

    ~MuxLengths() override = default;

   private:
    poly::optional<unsigned> splitPoint_;
    poly::optional<unsigned> matchLengthBias_;
};

} // namespace nodes
} // namespace openzl
