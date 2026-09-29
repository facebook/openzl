// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_transformer.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Graph.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"

namespace openzl {
namespace graphs {

class TransformerNumeric : public SimpleGraph<TransformerNumeric> {
   public:
    static constexpr GraphID graph = ZL_GRAPH_TRANSFORMER_NUMERIC;

    static constexpr GraphMetadata<1> metadata = {
        .inputs = { InputMetadata{ .typeMask = TypeMask::Numeric } },
        .lastInputIsVariable = true,
        .description =
                "Compress each numeric input by using a pretrained model to select the chain of transforms to apply",
    };
};

} // namespace graphs
} // namespace openzl
