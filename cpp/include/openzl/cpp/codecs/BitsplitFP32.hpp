// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_bitsplit_fp32.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"
#include "openzl/cpp/codecs/Node.hpp"

namespace openzl {
namespace nodes {
struct BitsplitFP32 : public SimplePipeNode<BitsplitFP32> {
   public:
    static constexpr NodeID node = ZL_NODE_BITSPLIT_FP32;

    static constexpr NodeMetadata<1, 0, 1> metadata = {
        .inputs          = { InputMetadata{ .type = Type::Numeric } },
        .variableOutputs = { OutputMetadata{ .type = Type::Numeric,
                                             .name = "bit_ranges" } },
        .description     = "Split float32 into sign, exponent, and mantissa"
    };
};
} // namespace nodes
} // namespace openzl
