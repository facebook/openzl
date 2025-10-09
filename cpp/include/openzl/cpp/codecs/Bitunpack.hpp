// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_bitunpack.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"
#include "openzl/cpp/codecs/Node.hpp"

namespace openzl {
namespace nodes {

class Bitunpack : public Node {
   public:
    static constexpr NodeID node = ZL_NODE_BITUNPACK;

    static constexpr NodeMetadata<1, 1> metadata = {
        .inputs           = { InputMetadata{
                          .type = Type::Serial,
                          .name = "bitpacked",
        } },
        .singletonOutputs = { OutputMetadata{ .type = Type::Numeric,
                                              .name = "unpacked ints" } },
        .description      = "Unpack integers of a fixed bit-width",
    };

    explicit Bitunpack(int numBits) : numBits_(numBits) {}

    NodeID baseNode() const override
    {
        return node;
    }

    poly::optional<NodeParameters> parameters() const override
    {
        if (numBits_ < 0 || numBits_ > 64) {
            throw Exception("Bad num bits: " + std::to_string(numBits_));
        }
        LocalParams lp;
        lp.addIntParam(ZL_Bitunpack_numBits, numBits_);
        return NodeParameters{ .localParams = std::move(lp) };
    }

    GraphID operator()(Compressor& compressor, GraphID unpacked) const
    {
        return buildGraph(compressor, { &unpacked, 1 });
    }

    ~Bitunpack() override = default;

   private:
    int numBits_;
};

} // namespace nodes
} // namespace openzl
