// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_sentinel.h"
#include "openzl/codecs/zl_store.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"
#include "openzl/cpp/codecs/Node.hpp"

#include <vector>

namespace openzl {
namespace nodes {

/**
 * Sentinel-byte node: narrows values < 255 to 1 byte, stores values >= 255
 * in a full-width exceptions stream with sentinel marker 255.
 */
struct SentinelByte : public Node {
    static constexpr NodeID node = ZL_NODE_SENTINEL_BYTE;

    static constexpr NodeMetadata<1, 2> metadata = {
        .inputs           = { InputMetadata{ .type = Type::Numeric } },
        .singletonOutputs = { OutputMetadata{ .type = Type::Numeric,
                                              .name = "values" },
                              OutputMetadata{ .type = Type::Numeric,
                                              .name = "exceptions" } },
        .description =
                "Narrow values < 255 to 1 byte, route exceptions at full width",
    };

    NodeID baseNode() const override
    {
        return node;
    }

    GraphID
    operator()(Compressor& compressor, GraphID values, GraphID exceptions) const
    {
        return buildGraph(
                compressor,
                std::initializer_list<GraphID>{ values, exceptions });
    }

    ~SentinelByte() override = default;
};

/**
 * General sentinel node: marks designated exception positions with a sentinel
 * value and routes original values to an exceptions stream.
 *
 * This node requires local params (exception indices + sentinel value) and
 * is intended to be invoked via run() within a function graph.
 */
class Sentinel : public Node {
   public:
    explicit Sentinel(
            poly::span<const size_t> exceptionIndices,
            poly::optional<uint64_t> sentinel = poly::nullopt)
            : exceptionIndices_(exceptionIndices), sentinel_(sentinel)
    {
    }

    static constexpr NodeID node = ZL_NODE_SENTINEL_NUM;

    static constexpr NodeMetadata<1, 2> metadata = {
        .inputs           = { InputMetadata{ .type = Type::Numeric } },
        .singletonOutputs = { OutputMetadata{ .type = Type::Numeric,
                                              .name = "values" },
                              OutputMetadata{ .type = Type::Numeric,
                                              .name = "exceptions" } },
        .description =
                "Replace values at exception indices with a sentinel marker",
    };

    NodeID baseNode() const override
    {
        return node;
    }

    poly::optional<NodeParameters> parameters() const override
    {
        // Use copy params instead of ref params. When using in runNode copy
        // params aren't actually copied, but when using this at graph
        // construction time they are (and have to be).
        LocalParams params;
        params.addCopyParam(
                ZL_SENTINEL_INDICES_PID,
                exceptionIndices_.data(),
                exceptionIndices_.size_bytes());
        if (sentinel_.has_value()) {
            params.addCopyParam(
                    ZL_SENTINEL_VALUE_PID,
                    &sentinel_.value(),
                    sizeof(uint64_t));
        }
        return NodeParameters{ .localParams = std::move(params) };
    }

    Edge::RunNodeResult run(Edge& edge) const override
    {
        return edge.runNode(baseNode(), parameters());
    }

    GraphID
    operator()(Compressor& compressor, GraphID values, GraphID exceptions) const
    {
        return buildGraph(
                compressor,
                std::initializer_list<GraphID>{ values, exceptions });
    }

    ~Sentinel() override = default;

   private:
    poly::span<const size_t> exceptionIndices_;
    poly::optional<uint64_t> sentinel_;
};

} // namespace nodes
} // namespace openzl
