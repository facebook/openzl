// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/lz/lz_compressor.h"

#include <vector>

#include "openzl/shared/xxhash.h"

namespace openzl {
namespace training {
namespace {

/// Hasher to implement `LzCompressor::hash()`.
class Hasher {
   public:
    Hasher()
    {
        XXH3_64bits_reset(&state_);
    }

    void update(uint64_t value)
    {
        XXH3_64bits_update(&state_, &value, sizeof(value));
    }

    void update(const poly::optional<int>& value)
    {
        update(uint64_t(value.has_value()));
        if (value.has_value()) {
            update(uint64_t(*value));
        }
    }

    void update(const poly::optional<ZL_LzStrategy>& value)
    {
        update(uint64_t(value.has_value()));
        if (value.has_value()) {
            update(uint64_t(*value));
        }
    }

    void update(const poly::optional<ACECompressor>& value)
    {
        update(uint64_t(value.has_value()));
        if (value.has_value()) {
            update(value->hash());
        }
    }

    uint64_t digest() const
    {
        return XXH3_64bits_digest(&state_);
    }

   private:
    XXH3_state_t state_{};
};

std::vector<ACECompressor> makeLiteralsCompressors()
{
    return std::vector<ACECompressor>{
        ACECompressor{ ACEGraph(graphs::Store{}) },
        ACECompressor{ ACEGraph(graphs::Huffman{}) },
        ACECompressor{ ACEGraph(graphs::Entropy{}) },
        ACECompressor{ ACEGraph(graphs::HuffmanPivCo{}) },
        ACECompressor{ ACEGraph(graphs::Bitpack{}) },
        ACECompressor{ ACEGraph(graphs::Flatpack{}) },
    };
}

std::vector<ACECompressor> makeOffsetsCompressors()
{
    std::vector<ACECompressor> compressors = {
        ACECompressor{ ACEGraph(graphs::Store{}) },
        ACECompressor{ ACEGraph(graphs::Bitpack{}) },
        ACECompressor{ ACEGraph(graphs::FieldLz{}) },
        ACECompressor{ ACEGraph(graphs::TransformerNumeric{}) },
    };
    for (int level = 1; level < 7; ++level) {
        compressors.emplace_back(ACEGraph(
                graphs::FieldLz{ graphs::FieldLz::Parameters{
                        .compressionLevel = level } }));
    }
    return compressors;
}

std::vector<ACECompressor> makeMuxedBytesCompressors()
{
    std::vector<ACECompressor> compressors = {
        ACECompressor{ ACEGraph(graphs::Store{}) },
        ACECompressor{ ACEGraph(graphs::Huffman{}) },
        ACECompressor{ ACEGraph(graphs::Entropy{}) },
        ACECompressor{ ACEGraph(graphs::HuffmanPivCo{}) },
        ACECompressor{ ACEGraph(graphs::Bitpack{}) },
        ACECompressor{ ACEGraph(graphs::Flatpack{}) },
    };
    compressors.push_back(
            ACECompressor{ ACENode(nodes::ConvertSerialToNum8{}),
                           { ACECompressor{ ACEGraph(
                                   graphs::TransformerNumeric{}) } } });
    return compressors;
}

std::vector<ACECompressor> makeOverflowLengthsCompressors()
{
    std::vector<ACECompressor> compressors = {
        ACECompressor{ ACEGraph(graphs::Store{}) },
        ACECompressor{ ACEGraph(graphs::Huffman{}) },
        ACECompressor{ ACEGraph(graphs::Entropy{}) },
        ACECompressor{ ACEGraph(graphs::Bitpack{}) },
        ACECompressor{ ACEGraph(graphs::TransformerNumeric{}) },
        ACECompressor{ ACEGraph(graphs::FieldLz{}) },
    };
    for (int level = 1; level < 7; ++level) {
        compressors.emplace_back(ACEGraph(
                graphs::FieldLz{ graphs::FieldLz::Parameters{
                        .compressionLevel = level } }));
    }
    return compressors;
}

std::vector<ACECompressor> makeMuxLengthsCompressors()
{
    return std::vector<ACECompressor>{
        ACECompressor{ ACEGraph(graphs::Store{}) },
        ACECompressor{ ACEGraph(graphs::Compress{}) },
        ACECompressor{ ACEGraph(graphs::TransformerNumeric{}) },
    };
}

} // namespace

// The pre-built lists are intentionally leaked, so that the spans handed out
// here stay valid however late they are used.

poly::span<const ACECompressor> literalsCompressors()
{
    static const auto* compressors =
            new std::vector<ACECompressor>(makeLiteralsCompressors());
    return *compressors;
}

poly::span<const ACECompressor> offsetsCompressors()
{
    static const auto* compressors =
            new std::vector<ACECompressor>(makeOffsetsCompressors());
    return *compressors;
}

poly::span<const ACECompressor> muxedBytesCompressors()
{
    static const auto* compressors =
            new std::vector<ACECompressor>(makeMuxedBytesCompressors());
    return *compressors;
}

poly::span<const ACECompressor> overflowLengthsCompressors()
{
    static const auto* compressors =
            new std::vector<ACECompressor>(makeOverflowLengthsCompressors());
    return *compressors;
}

poly::span<const ACECompressor> muxLengthsCompressors()
{
    static const auto* compressors =
            new std::vector<ACECompressor>(makeMuxLengthsCompressors());
    return *compressors;
}

uint64_t LzCompressor::hash() const
{
    Hasher h;
    h.update(params.nodeParams.compressionLevel);
    h.update(params.nodeParams.acceleration);
    h.update(params.nodeParams.windowLog);
    h.update(params.nodeParams.strategy);
    h.update(params.nodeParams.hashLog1);
    h.update(params.nodeParams.hashLog2);
    h.update(params.nodeParams.hashLength);
    h.update(params.nodeParams.searchLog);
    h.update(params.minGainForEntropyBytes);
    h.update(params.minGainForEntropyPct);
    h.update(literalsGraph);
    h.update(offsetsGraph);
    h.update(muxedBytesGraph);
    h.update(overflowLengthsGraph);
    h.update(muxLengthsGraph);
    return h.digest();
}

GraphParameters LzCompressor::buildParams(Compressor& compressor) const
{
    auto build = [&compressor](const poly::optional<ACECompressor>& successor) {
        poly::optional<GraphID> graph;
        if (successor.has_value()) {
            graph = successor->build(compressor);
        }
        return graph;
    };

    // Every graph field is owned by this gene, so they are all assigned here,
    // even the ones left to the LZ graph's defaults.
    auto p            = params;
    p.literalsGraph   = build(literalsGraph);
    p.offsetsGraph    = build(offsetsGraph);
    p.muxLengthsGraph = build(muxLengthsGraph);
    // The mux lengths node doesn't run when its graph is overridden, so neither
    // of its outputs exists to be compressed.
    p.muxedBytesGraph      = poly::nullopt;
    p.overflowLengthsGraph = poly::nullopt;
    if (!muxLengthsGraph.has_value()) {
        p.muxedBytesGraph      = build(muxedBytesGraph);
        p.overflowLengthsGraph = build(overflowLengthsGraph);
    }

    return graphs::Lz(std::move(p)).parameters().value();
}

} // namespace training
} // namespace openzl
