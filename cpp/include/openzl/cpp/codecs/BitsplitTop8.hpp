// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_bitsplit.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"
#include "openzl/cpp/codecs/Node.hpp"

namespace openzl {
namespace nodes {
struct BitsplitTop8 : public SimplePipeNode<BitsplitTop8> {
   public:
    static constexpr NodeID node = ZL_NODE_BITSPLIT_TOP8;

    static constexpr NodeMetadata<1, 0, 1> metadata = {
        .inputs          = { InputMetadata{ .type = Type::Numeric } },
        .variableOutputs = { OutputMetadata{ .type = Type::Numeric,
                                             .name = "bit_ranges" } },
        .description = "Split numeric into top 8 significant bits and remainder"
    };
};
} // namespace nodes
} // namespace openzl
