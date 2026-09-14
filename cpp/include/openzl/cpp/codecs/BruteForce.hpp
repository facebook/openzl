// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <initializer_list>
#include <utility>
#include <vector>

#include "openzl/codecs/zl_brute_force_selector.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/codecs/Graph.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"

namespace openzl {
namespace graphs {

class BruteForce : public Graph {
   public:
    static constexpr GraphID graph = ZL_GRAPH_BRUTE_FORCE;

    // TODO(terrelln): Work with multi-input graphs
    static constexpr GraphMetadata<1> metadata = {
        .inputs = { InputMetadata{ .typeMask = TypeMask::Any } },
        .description =
                "Try each successor graph and choose the one that "
                "produces the smallest compressed size",
    };

    explicit BruteForce(std::vector<GraphID> successors)
            : successors_(std::move(successors))
    {
        if (successors_.empty()) {
            throw Exception("BruteForce requires at least one successor");
        }
    }

    explicit BruteForce(std::initializer_list<GraphID> successors)
            : BruteForce(std::vector<GraphID>(successors))
    {
    }

    GraphID baseGraph() const override
    {
        return graph;
    }

    poly::optional<GraphParameters> parameters() const override
    {
        return GraphParameters{ .customGraphs = successors_ };
    }

   private:
    std::vector<GraphID> successors_;
};

} // namespace graphs
} // namespace openzl
