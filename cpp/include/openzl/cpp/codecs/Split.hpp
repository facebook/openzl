// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_split.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/FunctionGraph.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"
#include "openzl/cpp/codecs/Node.hpp"

namespace openzl {
namespace nodes {
namespace detail {
constexpr NodeMetadata<1, 0, 1> splitMetadata(Type type)
{
    return {
        .inputs          = { InputMetadata{ .type = type } },
        .variableOutputs = { OutputMetadata{ .type = type,
                                             .name = "segments" } },
        .description =
                "Split the input into N segments according to the given `segmentSizes`"
    };
}
} // namespace detail

class Split : public Node {
   public:
    explicit Split(poly::span<const size_t> segmentSizes)
            : segmentSizes_(segmentSizes)
    {
    }

    NodeID baseNode() const override
    {
        throw Exception("Split: Can only call run()");
    }

    Edge::RunNodeResult run(Edge& edge) const override
    {
        auto edges = unwrap(ZL_Edge_runSplitNode(
                edge.get(), segmentSizes_.data(), segmentSizes_.size()));
        return Edge::convert(edges);
    }

    ~Split() override = default;

   private:
    poly::span<const size_t> segmentSizes_;
};

class SplitSerial : public Split {
   public:
    static constexpr NodeMetadata<1, 0, 1> metadata =
            detail::splitMetadata(Type::Serial);

    using Split::Split;
};

class SplitNumeric : public Split {
   public:
    static constexpr NodeMetadata<1, 0, 1> metadata =
            detail::splitMetadata(Type::Numeric);

    using Split::Split;
};

class SplitStruct : public Split {
   public:
    static constexpr NodeMetadata<1, 0, 1> metadata =
            detail::splitMetadata(Type::Struct);

    using Split::Split;
};

class SplitString : public Split {
   public:
    static constexpr NodeMetadata<1, 0, 1> metadata =
            detail::splitMetadata(Type::String);

    using Split::Split;
};
class SplitByRange : public SimplePipeNode<SplitByRange> {
   public:
    static constexpr NodeID node = ZL_NODE_SPLIT_BYRANGE;

    static constexpr NodeMetadata<1, 0, 1> metadata = {
        .inputs          = { InputMetadata{ .type = Type::Numeric } },
        .variableOutputs = { OutputMetadata{ .type = Type::Numeric,
                                             .name = "range_segments" } },
        .description =
                "Split numeric input into segments at detected range boundaries"
    };

    SplitByRange() = default;
    explicit SplitByRange(int minSegmentSize) : minSegmentSize_(minSegmentSize)
    {
    }

    poly::optional<NodeParameters> parameters() const override
    {
        if (minSegmentSize_.has_value()) {
            LocalParams params;
            params.addIntParam(
                    ZL_SPLIT_BYRANGE_MIN_SEGMENT_SIZE_PID,
                    minSegmentSize_.value());
            return NodeParameters{ .localParams = std::move(params) };
        }
        return poly::nullopt;
    }

    ~SplitByRange() override = default;

   private:
    poly::optional<int> minSegmentSize_;
};

} // namespace nodes
namespace graphs {
struct Split {
    // TODO(terrelln): Make the split graph serializable by switching to a
    // dynamic graph.
    // - Call the correct split node based on the type
    // - Pass in the successors as customGraphs

    // GraphID operator()(
    //         Type type,
    //         poly::span<const size_t> segmentSizes,
    //         poly::span<const GraphID> successors) const
    // {
    // }
};
constexpr Split split;
} // namespace graphs
} // namespace openzl
