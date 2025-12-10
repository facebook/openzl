// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_lz4.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Graph.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"

namespace openzl {
namespace graphs {
struct Lz4 : public Graph {
   public:
    static constexpr GraphID graph = ZL_GRAPH_LZ4;

    static constexpr GraphMetadata<1> metadata = {
        .inputs      = { InputMetadata{ .typeMask = TypeMask::Serial } },
        .description = "Lz4 compress the input data",
    };

    Lz4() {}
    explicit Lz4(int compressionLevel) : compressionLevel_(compressionLevel) {}

    GraphID baseGraph() const override
    {
        return graph;
    }

    poly::optional<GraphParameters> parameters() const override
    {
        LocalParams lp;
        if (compressionLevel_.has_value()) {
            lp.addIntParam(
                    ZL_LZ4_COMPRESSION_LEVEL_OVERRIDE_PID,
                    compressionLevel_.value());
        }
        return GraphParameters{ .localParams = std::move(lp) };
    }

    ~Lz4() override = default;

   private:
    poly::optional<int> compressionLevel_;
};
} // namespace graphs
} // namespace openzl
