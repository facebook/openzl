// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_top8bits.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"
#include "openzl/cpp/codecs/Node.hpp"

namespace openzl {
namespace nodes {
struct Top8bits : public SimplePipeNode<Top8bits> {
   public:
    static constexpr NodeID node = ZL_NODE_TOP8BITS;

    static constexpr NodeMetadata<1, 0, 1> metadata = {
        .inputs          = { InputMetadata{ .type = Type::Numeric } },
        .variableOutputs = { OutputMetadata{ .type = Type::Numeric,
                                             .name = "bit_ranges" } },
        .description = "Split numeric into top 8 significant bits and remainder"
    };
};
} // namespace nodes
} // namespace openzl
