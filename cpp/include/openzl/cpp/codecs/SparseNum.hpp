// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_sparse_num.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"
#include "openzl/cpp/codecs/Node.hpp"

namespace openzl {
namespace nodes {

class SparseNum : public Node {
   public:
    static constexpr NodeID node = ZL_NODE_SPARSE_NUM;

    static constexpr NodeMetadata<1, 2> metadata = {
        .inputs = { InputMetadata{ .type = Type::Numeric, .name = "values" } },
        .singletonOutputs = { OutputMetadata{ .type = Type::Numeric,
                                              .name = "distances" },
                              OutputMetadata{ .type = Type::Numeric,
                                              .name = "values" } },
        .description =
                "Split a numeric stream into zero run distances and literal values",
    };

    SparseNum() {}

    NodeID baseNode() const override
    {
        return node;
    }

    GraphID
    operator()(Compressor& compressor, GraphID distances, GraphID values) const
    {
        return buildGraph(
                compressor,
                std::initializer_list<GraphID>{ distances, values });
    }

    ~SparseNum() override = default;
};

class SparseNumAuto : public Node {
   public:
    static constexpr NodeID node = ZL_NODE_SPARSE_NUM_AUTO;

    static constexpr NodeMetadata<1, 2> metadata = {
        .inputs = { InputMetadata{ .type = Type::Numeric, .name = "values" } },
        .singletonOutputs = { OutputMetadata{ .type = Type::Numeric,
                                              .name = "distances" },
                              OutputMetadata{ .type = Type::Numeric,
                                              .name = "values" } },
        .description =
                "Split a numeric stream into dominant-symbol run distances and literal values",
    };

    SparseNumAuto() {}
    explicit SparseNumAuto(uint64_t dominant) : dominant_(dominant) {}
    explicit SparseNumAuto(poly::optional<uint64_t> dominant)
            : dominant_(std::move(dominant))
    {
    }

    NodeID baseNode() const override
    {
        return node;
    }

    GraphID
    operator()(Compressor& compressor, GraphID distances, GraphID values) const
    {
        return buildGraph(
                compressor,
                std::initializer_list<GraphID>{ distances, values });
    }

    poly::optional<NodeParameters> parameters() const override
    {
        if (dominant_.has_value()) {
            LocalParams params;
            params.addCopyParam(
                    ZL_SPARSE_NUM_DOMINANT_VALUE_PID, dominant_.value());
            return NodeParameters{ .localParams = std::move(params) };
        }
        return poly::nullopt;
    }

    ~SparseNumAuto() override = default;

   private:
    poly::optional<uint64_t> dominant_;
};

} // namespace nodes
} // namespace openzl
