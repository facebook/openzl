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
                "Split a numeric stream into zero-run distances and literal values",
    };

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

} // namespace nodes
} // namespace openzl
