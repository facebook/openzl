// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "Benchmark.hpp"

#include "tools/logger/Logger.h"

namespace openzl::bench {

namespace {
using tools::logger::Logger;
using tools::logger::LogLevel;

template <typename Fn>
size_t benchmarkFn(
        std::vector<std::chrono::nanoseconds>& durations,
        size_t minIters,
        std::chrono::nanoseconds minTime,
        Fn&& fn)
{
    using Clock = std::chrono::steady_clock;

    // Run one warmup iteration
    auto firstResult = fn();

    auto benchUntil = Clock::now() + minTime;
    size_t iters    = 0;

    for (; iters < minIters || Clock::now() < benchUntil; ++iters) {
        auto start  = Clock::now();
        auto result = fn();
        auto stop   = Clock::now();
        if (result != firstResult) {
            throw std::runtime_error("Results differ!");
        }
        durations.push_back(stop - start);
    }

    return firstResult;
}

nlohmann::json toJson(const std::vector<std::chrono::nanoseconds>& durations)
{
    auto out = nlohmann::json::array();
    for (const auto& d : durations) {
        out.push_back(d.count());
    }
    return out;
}

std::string trunc(std::string str, size_t n, bool left)
{
    if (n < 3) {
        throw std::runtime_error("bad n");
    }
    if (str.size() > n) {
        size_t len = n - 3;
        if (left) {
            str = "..." + str.substr(str.size() - len);
        } else {
            str = str.substr(0, len) + "...";
        }
        return str;
    } else {
        return str;
    }
}
} // namespace

nlohmann::json BlockResult::json() const
{
    nlohmann::json data;
    data["original_size"]                = originalSize;
    data["compressed_size"]              = compressedSize;
    data["best_compression_duration_ns"] = bestCompressionDuration().count();
    data["best_decompression_duration_ns"] =
            bestDecompressionDuration().count();
    data["compression_durations_ns"]   = toJson(compressionDurations);
    data["decompression_durations_ns"] = toJson(decompressionDurations);
    return data;
}

nlohmann::json BenchmarkResult::json() const
{
    nlohmann::json data;
    data["file_name"]                    = fileName;
    data["compressor_name"]              = compressorName;
    data["compressor_config"]            = compressorConfig;
    data["original_size"]                = originalSize();
    data["compressed_size"]              = compressedSize();
    data["compression_ratio"]            = compressionRatio();
    data["best_compression_duration_ns"] = bestCompressionDuration().count();
    data["best_decompression_duration_ns"] =
            bestDecompressionDuration().count();
    data["best_compression_speed_mbps"]   = bestCompressionSpeedMBps();
    data["best_decompression_speed_mbps"] = bestDecompressionSpeedMBps();
    data["blocks"]                        = nlohmann::json::array();
    for (const auto& block : blockResults) {
        data["blocks"].push_back(block.json());
    }
    return data;
}

/* static */ std::string BenchmarkResult::header()
{
    char buffer[200];
    int len = snprintf(
            buffer,
            sizeof(buffer),
            "%20s, %40s, %10s, %20s, %20s",
            "File",
            "Compressor",
            "Ratio",
            "Compression Speed",
            "Decompression Speed");
    if (len < 0 || len >= sizeof(buffer)) {
        throw std::runtime_error("bad format");
    }
    return buffer;
}

std::string BenchmarkResult::pretty() const
{
    char buffer[200];
    int len = snprintf(
            buffer,
            sizeof(buffer),
            "%20s, %40s, %10.2f, %15.1f MB/s, %15.1f MB/s",
            trunc(fileName, 20, true).c_str(),
            trunc(compressorName, 40, false).c_str(),
            compressionRatio(),
            bestCompressionSpeedMBps(),
            bestDecompressionSpeedMBps());
    if (len < 0 || len >= sizeof(buffer)) {
        throw std::runtime_error("bad format");
    }
    return buffer;
}

std::vector<BenchmarkResult> benchmark(
        const tools::io::InputSet& inputs,
        const BenchmarkArgs& args)
{
    if (args.minIters == 0) {
        throw std::runtime_error("min iters must be at least 1");
    }
    for (const auto& compressorConfig : args.compressorConfigs) {
        // Ensure each compressor builds
        makeCompressor(compressorConfig);
    }

    Logger::log_c(LogLevel::INFO, "%s", BenchmarkResult::header().c_str());
    std::vector<BenchmarkResult> results;
    std::unordered_map<nlohmann::json, BenchmarkResult> summaryResults;
    for (const auto& input : inputs) {
        for (const auto& compressorConfig : args.compressorConfigs) {
            auto compressor = makeCompressor(compressorConfig);
            auto data       = input->contents();

            BenchmarkResult result;
            result.fileName         = input->name();
            result.compressorName   = compressor->name();
            result.compressorConfig = compressorConfig;

            const auto blockSize   = args.blockSize.value_or(data.size());
            const size_t numBlocks = (data.size() + blockSize - 1) / blockSize;
            const auto minTimePerBlock = args.minTime / numBlocks;

            do {
                auto block = data.substr(0, blockSize);
                BlockResult blockResult;
                blockResult.originalSize = block.size();
                auto cCapacity           = compressor->compressBound(block);
                auto cBuf                = std::make_unique<char[]>(cCapacity);
                auto dBuf = std::make_unique<char[]>(block.size());

                blockResult.compressedSize = benchmarkFn(
                        blockResult.compressionDurations,
                        args.mode == BenchmarkMode::Decompression
                                ? 0
                                : args.minIters,
                        args.mode == BenchmarkMode::Decompression
                                ? std::chrono::seconds(0)
                                : minTimePerBlock,
                        [&] {
                            return compressor->compress(
                                    { cBuf.get(), cCapacity }, block);
                        });

                auto dSize = benchmarkFn(
                        blockResult.decompressionDurations,
                        args.mode == BenchmarkMode::Compression ? 0
                                                                : args.minIters,
                        args.mode == BenchmarkMode::Compression
                                ? std::chrono::seconds(0)
                                : minTimePerBlock,
                        [&] {
                            return compressor->decompress(
                                    { dBuf.get(), block.size() },
                                    { cBuf.get(), blockResult.compressedSize });
                        });

                if (poly::string_view{ dBuf.get(), dSize } != block) {
                    for (size_t i = 0; i < block.size(); ++i) {
                        if (dBuf[i] != block[i]) {
                            throw std::runtime_error(
                                    "Corruption: pos " + std::to_string(i));
                        }
                    }
                }
                result.blockResults.push_back(blockResult);
                data = data.substr(block.size());
            } while (!data.empty());
            Logger::log_c(LogLevel::INFO, "%s", result.pretty().c_str());
            results.push_back(result);
            summaryResults[compressorConfig] += result;
        }
    }
    for (const auto& [compressorConfig, result] : summaryResults) {
        Logger::log_c(LogLevel::INFO, "%s", result.pretty().c_str());
    }
    return results;
}

} // namespace openzl::bench
