// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_segmenters.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Graph.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"

namespace openzl {
namespace graphs {

class SegmentSerial : public Graph {
   public:
    static constexpr GraphID graph = ZL_SEGMENT_SERIAL;

    static constexpr GraphMetadata<1> metadata = {
        .inputs = { InputMetadata{ .typeMask = TypeMask::Serial } },
        .description =
                "Auto-segmenting graph for serial inputs. Chunks the input into "
                "independently compressed segments and forwards each chunk to a "
                "successor graph (defaults to ZL_GRAPH_COMPRESS_GENERIC). "
                "Defaults to 16 MiB chunking when no chunk-size hint is provided; "
                "treats a 0 hint as the default chunk size.",
    };

    explicit SegmentSerial(GraphID successor, size_t chunkByteSize = 0)
            : successor_(successor), chunkByteSize_(chunkByteSize)
    {
    }

    ~SegmentSerial() override = default;

    SegmentSerial(const SegmentSerial&)            = default;
    SegmentSerial& operator=(const SegmentSerial&) = default;
    SegmentSerial(SegmentSerial&&)                 = default;
    SegmentSerial& operator=(SegmentSerial&&)      = default;

    GraphID baseGraph() const override
    {
        return graph;
    }

    /// Delegates to ZL_Compressor_buildSerialSegmenter2 so the C builder
    /// is the single source of truth for chunkByteSize validation, default
    /// substitution, and the resulting parameter_invalid error code.
    GraphID parameterize(Compressor& compressor) const override
    {
        return compressor.unwrap(ZL_Compressor_buildSerialSegmenter2(
                compressor.get(), chunkByteSize_, successor_));
    }

    /// Used by setMultiInputDestination in FunctionGraph contexts. Mirrors
    /// the C builder's wire-level parameterization but without validation —
    /// callers in that path must pass a chunkByteSize that fits in int.
    poly::optional<GraphParameters> parameters() const override
    {
        LocalParams lp;
        if (chunkByteSize_ != 0) {
            lp.addIntParam(
                    ZL_SEGMENT_SERIAL_CHUNK_BYTE_SIZE_PARAM,
                    static_cast<int>(chunkByteSize_));
        }
        return GraphParameters{ .customGraphs = { { successor_ } },
                                .localParams  = std::move(lp) };
    }

   private:
    GraphID successor_;
    size_t chunkByteSize_;
};

} // namespace graphs
} // namespace openzl
