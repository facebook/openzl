// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <climits>
#include <string>

#include "openzl/codecs/zl_sddl2.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/Exception.hpp"
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
                "Auto-segmenting graph that runs the Simple Data Description Language v2 over the input. Must be given bytecode and successor; defaults to 16 MiB chunking when no chunk-size hint is provided, and also treats a 0 hint as the default chunk size. Refer to the SDDL documentation for usage instructions.",
    };

    explicit SDDL2(
            poly::string_view bytecode,
            GraphID successor,
            size_t chunkByteSize = 0)
            : bytecode_(bytecode),
              successor_(successor),
              chunkByteSize_(validateChunkByteSize(chunkByteSize))
    {
    }

    ~SDDL2() override = default;

    SDDL2(const SDDL2&)            = default;
    SDDL2& operator=(const SDDL2&) = default;
    SDDL2(SDDL2&&)                 = default;
    SDDL2& operator=(SDDL2&&)      = default;

    GraphID baseGraph() const override
    {
        return graph;
    }

    poly::optional<GraphParameters> parameters() const override
    {
        LocalParams lp;
        lp.addCopyParam(
                SDDL2_BYTECODE_PARAM, bytecode_.data(), bytecode_.size());
        if (chunkByteSize_ != 0) {
            lp.addIntParam(SDDL2_CHUNK_BYTE_SIZE_PARAM, chunkByteSize_);
        }
        return GraphParameters{ .customGraphs = { { successor_ } },
                                .localParams  = std::move(lp) };
    }

   private:
    static int validateChunkByteSize(size_t chunkByteSize)
    {
        if (chunkByteSize > static_cast<size_t>(INT_MAX)) {
            throw Exception(
                    "Bad SDDL2 chunk size: " + std::to_string(chunkByteSize));
        }
        return static_cast<int>(chunkByteSize);
    }

    poly::string_view bytecode_;
    GraphID successor_;
    int chunkByteSize_;
};

} // namespace graphs
} // namespace openzl
