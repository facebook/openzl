// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdint>

#include "openzl/openzl.hpp"

#include "openzl/cpp/codecs/Lz.hpp"
#include "tools/training/ace/ace_compressor.h"
#include "tools/training/utils/mutation.h"

namespace openzl {
namespace training {

/**
 * A tuning of a single LZ backend graph, which is the gene evolved by
 * `LzGeneticAlgorithm`.
 *
 * The backend graphs are held as `ACECompressor`s rather than in `params`
 * because a `GraphID` is only meaningful inside the `Compressor` it was built
 * into. `buildParams()` builds them and fills in the graph fields of `params`,
 * so those fields must be left unset here.
 */
struct LzCompressor {
    graphs::Lz::Parameters params;
    poly::optional<ACECompressor> literalsGraph;
    poly::optional<ACECompressor> offsetsGraph;
    poly::optional<ACECompressor> muxedBytesGraph;
    poly::optional<ACECompressor> overflowLengthsGraph;
    /**
     * Compresses the literal lengths & match lengths together, in place of the
     * mux lengths node. The muxed bytes and overflow lengths graphs are unused
     * when this is set, because those streams are only produced by the mux
     * lengths node.
     */
    poly::optional<ACECompressor> muxLengthsGraph;

    uint64_t hash() const;

    bool operator==(const LzCompressor& other) const
    {
        // Hash equality, for the same reasons as ACECompressor: collisions are
        // both unlikely and harmless here.
        return hash() == other.hash();
    }

    bool operator!=(const LzCompressor& other) const
    {
        return !(*this == other);
    }

    /// Builds the backend graphs into @p compressor and @returns the parameters
    /// which select them on the LZ graph.
    GraphParameters buildParams(Compressor& compressor) const;
};

/// @returns the backend graphs to choose between for the LZ literals output.
poly::span<const ACECompressor> literalsCompressors();
/// @returns the backend graphs to choose between for the LZ offsets output.
poly::span<const ACECompressor> offsetsCompressors();
/// @returns the backend graphs to choose between for the muxed bytes output.
poly::span<const ACECompressor> muxedBytesCompressors();
/// @returns the backend graphs to choose between for the overflow lengths
/// output.
poly::span<const ACECompressor> overflowLengthsCompressors();
/// @returns the backend graphs to choose between for the literal lengths and
/// match lengths together. These must accept two numeric inputs, which rules
/// out every compressor rooted at a node, since nodes take a single input.
poly::span<const ACECompressor> muxLengthsCompressors();

/// A mutation which retunes an LZ backend graph in place.
class LzGraphMutation : public BackendGraphMutation {
   public:
    LzGraphMutation(std::string lzGraph, LzCompressor lz)
            : BackendGraphMutation(std::move(lzGraph)), lz_(std::move(lz))
    {
    }

    poly::optional<GraphParameters> newGraphParams(
            Compressor& compressor) const override
    {
        return lz_.buildParams(compressor);
    }

    const LzCompressor& lz() const
    {
        return lz_;
    }

   private:
    LzCompressor lz_;
};

} // namespace training
} // namespace openzl

namespace std {
template <>
struct hash<openzl::training::LzCompressor> {
    size_t operator()(const openzl::training::LzCompressor& lz) const
    {
        return lz.hash();
    }
};
} // namespace std
