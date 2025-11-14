// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/compress/graphs/sddl2/sddl2.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Graph.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"
#include "openzl/cpp/poly/StringView.hpp"

namespace openzl {
namespace graphs {

class SDDL2 : public Graph {
   public:
    static constexpr GraphID graph = ZL_GRAPH_SDDL2;

    static constexpr GraphMetadata<1> metadata = {
        .inputs = { InputMetadata{ .typeMask = TypeMask::Serial } },
        .description =
                "Graph that runs the Simple Data Description Language v2 over the input to decompose the input stream into a number of output streams. Must be given bytecode and successor. Refer to the SDDL documentation for usage instructions.",
    };

    explicit SDDL2(poly::string_view bytecode, GraphID successor)
            : bytecode_(bytecode), successor_(successor)
    {
    }

    GraphID baseGraph() const override
    {
        return graph;
    }

    poly::optional<GraphParameters> parameters() const override
    {
        LocalParams lp;
        lp.addCopyParam(
                SDDL2_BYTECODE_PARAM,
                bytecode_.data(),
                bytecode_.size());
        return GraphParameters{ .customGraphs = { { successor_ } },
                                .localParams  = std::move(lp) };
    }

    ~SDDL2() override = default;

   private:
    poly::string_view bytecode_;
    GraphID successor_;
};

} // namespace graphs
} // namespace openzl
