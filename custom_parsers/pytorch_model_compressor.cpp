// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <stdio.h>
#include <chrono>

#include <folly/FileUtil.h>
#include <folly/init/Init.h>

#include "custom_parsers/pytorch_model_parser.h"
#include "tools/zstrong_cpp.h"

namespace {
constexpr size_t kRepeats = 10;
}

int main(int argc, char** argv)
{
    auto init = folly::Init(&argc, &argv);
    if (argc < 2 || argc > 3) {
        return 1;
    }

    std::string src;
    if (!folly::readFile(argv[1], src)) {
        fprintf(stderr, "Failed to read input file: %s\n", argv[2]);
        return 1;
    }

    zstrong::CCtx cctx;
    zstrong::CGraph cgraph;

    zstrong::DCtx dctx;

    const auto graphID = ZL_createGraph_pytorchModelCompressor(cgraph.get());

    std::string compressed(ZL_compressBound(src.size()), '\0');
    try {
        cgraph.unwrap(ZL_Compressor_setParameter(
                cgraph.get(), ZL_CParam_formatVersion, 14));
        cgraph.unwrap(
                ZL_Compressor_selectStartingGraphID(cgraph.get(), graphID));
        cctx.unwrap(ZL_CCtx_refCompressor(cctx.get(), cgraph.get()));
        const auto cSize = cctx.unwrap(ZL_CCtx_compress(
                cctx.get(),
                compressed.data(),
                compressed.size(),
                src.data(),
                src.size()));
        compressed.resize(cSize);
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to compress: %s\n", e.what());
        return 1;
    }

    std::string roundTripped(src.size(), '\0');
    try {
        const auto roundTrippedSize = dctx.unwrap(ZL_DCtx_decompress(
                dctx.get(),
                roundTripped.data(),
                roundTripped.size(),
                compressed.data(),
                compressed.size()));
        roundTripped.resize(roundTrippedSize);
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to decompress: %s\n", e.what());
        return 1;
    }

    if (src != roundTripped) {
        fprintf(stderr, "Round-trip failed\n");
        return 1;
    }

    std::string outFile = argc == 3 ? argv[2] : (std::string(argv[1]) + ".zs");
    if (!folly::writeFile(compressed, outFile.c_str())) {
        fprintf(stderr, "Failed to write output file: %s\n", outFile.c_str());
        return 1;
    }

    std::chrono::nanoseconds compressNs{ 0 };
    std::chrono::nanoseconds decompressNs{ 0 };
    size_t uncompressedBytes = 0;

    for (size_t i = 0; i < kRepeats; ++i) {
        cctx.unwrap(ZL_CCtx_refCompressor(cctx.get(), cgraph.get()));
        auto start       = std::chrono::steady_clock::now();
        const auto cSize = cctx.unwrap(ZL_CCtx_compress(
                cctx.get(),
                compressed.data(),
                compressed.size(),
                src.data(),
                src.size()));
        auto stop        = std::chrono::steady_clock::now();
        compressNs += (stop - start);

        start = std::chrono::steady_clock::now();

        const auto roundTrippedSize = dctx.unwrap(ZL_DCtx_decompress(
                dctx.get(),
                roundTripped.data(),
                roundTripped.size(),
                compressed.data(),
                cSize));

        stop = std::chrono::steady_clock::now();
        decompressNs += (stop - start);

        uncompressedBytes += roundTrippedSize;
    }

    const auto compressionSpeed =
            double(uncompressedBytes) / compressNs.count() * 1000.0;
    const auto decompressionSpeed =
            double(uncompressedBytes) / decompressNs.count() * 1000.0;

    fprintf(stderr, "Original size      : %zu\n", src.size());
    fprintf(stderr, "Compressed size    : %zu\n", compressed.size());
    fprintf(stderr,
            "Compression ratio  : %.2f\n",
            double(src.size()) / double(compressed.size()));
    fprintf(stderr, "Compression speed  : %.2f\n", compressionSpeed);
    fprintf(stderr, "Decompression speed: %.2f\n", decompressionSpeed);

    return 0;
}
