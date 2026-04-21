// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_lz.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"
#include "openzl/cpp/codecs/Node.hpp"

namespace openzl {
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

    Lz() {}

    NodeID baseNode() const override
    {
        return node;
    }

    ~Lz() override = default;
};
} // namespace nodes
} // namespace openzl
