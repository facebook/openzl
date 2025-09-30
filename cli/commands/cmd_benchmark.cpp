// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cli/commands/cmd_benchmark.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/Exception.hpp"
#include "openzl/zl_compress.h"

#include "cli/utils/util.h"
#include "tools/io/OutputNull.h"
#include "tools/logger/Logger.h"

namespace openzl::cli {

using namespace openzl::tools::logger;

namespace {
constexpr size_t BYTES_TO_MB  = 1000 * 1000;
constexpr size_t BYTES_TO_GiB = 1024 * 1024 * 1024;

/// Updates the printed line of benchmarks based on the new parameters provided.
/// @return The BenchmarkResult structure containing ratio and speeds
BenchmarkResult updateResults(
        size_t input_count,
        size_t iter_count,
        size_t compressed_size,
        size_t uncompressed_size,
        std::chrono::nanoseconds cdur,
        std::chrono::nanoseconds ddur)
{
    const auto ratio = static_cast<double>(uncompressed_size) / compressed_size;

    const auto cmicros = std::chrono::duration<double, std::micro>(cdur);
    const auto dmicros = std::chrono::duration<double, std::micro>(ddur);

    const auto cmibps = (uncompressed_size * iter_count * 1000 * 1000.0)
            / (cmicros.count() * BYTES_TO_MB);
    const auto dmibps = (uncompressed_size * iter_count * 1000 * 1000.0)
            / (dmicros.count() * BYTES_TO_MB);

    Logger::update(
            INFO,
            "%zu files: %zu -> %zu (%.2f),  %.2f MB/s  %.2f MB/s",
            input_count,
            uncompressed_size,
            compressed_size,
            ratio,
            cmibps,
            dmibps);
    // TODO fix
    return (BenchmarkResult){
        .compressionRatio   = ratio,
        .decompressionSpeed = dmibps,
        .compressionSpeed   = cmibps,
    };
}

/**
 * Helper function to create a compression context with the given compressor and
 * level.
 */
CCtx createCompressionContext(
        const Compressor& compressor,
        const std::optional<int>& level,
        bool strict)
{
    // create compression context
    CCtx cctx;

    cctx.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    cctx.setParameter(CParam::StickyParameters, 1);
    if (!strict) {
        cctx.setParameter(CParam::PermissiveCompression, 1);
    }
    if (level.has_value()) {
        cctx.setParameter(CParam::CompressionLevel, level.value());
    }
    cctx.refCompressor(compressor);

    return cctx;
}
} // namespace

int cmdBenchmark(const BenchmarkArgs& args)
{
    (void)runCompressionBenchmarks(args);
    return 0;
}

BenchmarkResult runCompressionBenchmarks(const BenchmarkArgs& args)
{
    BenchmarkResult finalResult{};
    const auto iters = args.numIters;

    // create compressor, context, and decompression context
    auto cctx =
            createCompressionContext(*args.compressor, args.level, args.strict);
    DCtx dctx;

    // if output is not specified, don't write csv-formatted summary statistics
    tools::io::OutputNull devnull{};
    tools::io::Output& csvOutput = (args.outputCsv) ? *args.outputCsv : devnull;
    csvOutput.open();
    std::ostream& csvOut = csvOutput.get_ostream();
    csvOut << "srcSize,compressedSize,compressionRatio,ctimeMs,dtimeMs,iters,path"
           << std::endl;

    // benchmark compression and decompression
    auto cdur                      = std::chrono::nanoseconds::zero();
    auto ddur                      = std::chrono::nanoseconds::zero();
    size_t total_compressed_size   = 0;
    size_t total_uncompressed_size = 0;
    size_t total_inputs            = 0;
    for (const auto& inputs : args.inputs) {
        auto& inputVec = *inputs;
        total_inputs++;
        size_t uncompressed_size = 0;
        for (const auto& input : *inputs) {
            uncompressed_size += input.contentSize();
        }
        // TODO: Size limitations should be a library feature
        if (uncompressed_size > 2 * BYTES_TO_GiB) {
            throw std::runtime_error(
                    "Chunking support is required for compressing inputs larger than 2 GiB. ");
        }
        // get the compressed size
        const auto compressed = cctx.compress(inputVec);

        util::logWarnings(cctx);

        total_compressed_size += compressed.size();
        total_uncompressed_size += uncompressed_size;

        // benchmark compression and decompression speeds
        const auto compression_start = std::chrono::steady_clock::now();
        for (size_t n = 0; n < iters; ++n) {
            const auto curr_compressed_size = cctx.compress(inputVec).size();

            util::logWarnings(cctx);

            if (curr_compressed_size != compressed.size()) {
                throw std::runtime_error("Non-deterministic compression!");
            }
        }
        const auto compression_end = std::chrono::steady_clock::now();

        // benchmark decompression
        const auto decompression_start = std::chrono::steady_clock::now();
        for (size_t n = 0; n < iters; ++n) {
            auto decompressed = dctx.decompress(compressed);
            for (size_t i = 0; i < decompressed.size(); ++i) {
                if (decompressed[i].contentSize()
                    != inputVec[i].contentSize()) {
                    throw std::runtime_error("Round-trip failure!");
                }
            }
            util::logWarnings(dctx);
        }
        const auto decompression_end = std::chrono::steady_clock::now();
        cdur += compression_end - compression_start;
        ddur += decompression_end - decompression_start;
        finalResult = updateResults(
                total_inputs,
                iters,
                total_compressed_size,
                total_uncompressed_size,
                cdur,
                ddur);
        csvOut << uncompressed_size << "," << compressed.size() << ","
               << finalResult.compressionRatio << ","
               << std::chrono::duration<double, std::milli>(
                          compression_end - compression_start)
                          .count()
               << ","
               << std::chrono::duration<double, std::milli>(
                          decompression_end - decompression_start)
                          .count()
               << "," << iters << std::endl;
    }
    // Finish the benchmark line.
    Logger::finalizeUpdate(INFO);

    if (total_inputs == 0) {
        throw InvalidArgsException("No samples found in inputs");
    }
    return finalResult;
}

} // namespace openzl::cli
