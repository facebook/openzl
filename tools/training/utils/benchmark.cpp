// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/utils/benchmark.h"

#include <memory>

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/FatBundleDictLoader.hpp"

namespace openzl {
namespace training {
namespace {
poly::optional<CompressionResult> benchmarkWithContexts(
        CCtx& cctx,
        DCtx& dctx,
        const Compressor& compressor,
        poly::span<const MultiInput> inputs)
{
    CompressionResult result{};
    for (const auto& input : inputs) {
        // TODO: For non-string inputs pre-reserve IO buffers
        std::string compressed;
        auto cStart = std::chrono::steady_clock::now();
        try {
            cctx.refCompressor(compressor);
            compressed = cctx.compress(*input);
        } catch (const Exception&) {
            return poly::nullopt;
        }
        auto cStop = std::chrono::steady_clock::now();

        auto dStart       = std::chrono::steady_clock::now();
        auto roundTripped = dctx.decompress(compressed);
        auto dStop        = std::chrono::steady_clock::now();

        if (roundTripped.size() != input->size()) {
            throw Exception("Bad round trip!");
        }
        for (size_t i = 0; i < roundTripped.size(); ++i) {
            if (roundTripped[i] != input->at(i)) {
                throw std::runtime_error("Bad round trip!");
            }
        }

        size_t originalSize = 0;
        for (const auto& i : *input) {
            originalSize += i.contentSize();
            if (i.type() == Type::String) {
                originalSize += i.numElts() * sizeof(uint32_t);
            }
        }

        result += CompressionResult{
            .originalSize      = originalSize,
            .compressedSize    = compressed.size(),
            .compressionTime   = cStop - cStart,
            .decompressionTime = dStop - dStart,
        };
    }

    return result;
}

std::vector<MultiInput> toMultiInputs(poly::span<const Input> inputs)
{
    std::vector<MultiInput> multiInputs;
    multiInputs.reserve(inputs.size());
    for (const auto& input : inputs) {
        MultiInput multiInput;
        multiInput.add(InputRef{ const_cast<ZL_Input*>(input.get()) });
        multiInputs.push_back(std::move(multiInput));
    }
    return multiInputs;
}
} // namespace

poly::optional<CompressionResult> benchmark(
        const Compressor& compressor,
        poly::span<const MultiInput> inputs,
        poly::string_view dictBundleData)
{
    CCtx cctx;
    std::unique_ptr<FatBundleDictLoader> dictLoader;
    DCtx dctx;
    if (!dictBundleData.empty()) {
        dictLoader = std::make_unique<FatBundleDictLoader>();
        dictLoader->loadFatBundle(dictBundleData);
        dctx.refDictLoader(*dictLoader);
    }
    return benchmarkWithContexts(cctx, dctx, compressor, inputs);
}

poly::optional<CompressionResult> benchmark(
        const Compressor& compressor,
        poly::span<const Input> inputs,
        poly::string_view dictBundleData)
{
    const auto multiInputs = toMultiInputs(inputs);
    return benchmark(compressor, multiInputs, dictBundleData);
}

poly::optional<CompressionResult> benchmark(
        CCtx& cctx,
        DCtx& dctx,
        const Compressor& compressor,
        poly::span<const Input> inputs)
{
    const auto multiInputs = toMultiInputs(inputs);
    return benchmarkWithContexts(cctx, dctx, compressor, multiInputs);
}
} // namespace training
} // namespace openzl
