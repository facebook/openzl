// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include "tools/io/InputSet.h"
#include "tools/json.hpp"

#include "Compressor.hpp"

namespace openzl::bench {

enum class BenchmarkMode {
    Both,
    Compression,
    Decompression,
};

struct BenchmarkArgs {
    size_t minIters{ 1 };
    std::chrono::nanoseconds minTime{ 1 };
    BenchmarkMode mode{ BenchmarkMode::Both };
    std::vector<nlohmann::json> compressorConfigs;
};

struct BenchmarkResult {
    std::string fileName;
    std::string compressorName;
    nlohmann::json compressorConfig;
    size_t originalSize;
    size_t compressedSize;
    std::vector<std::chrono::nanoseconds> compressionDurations;
    std::vector<std::chrono::nanoseconds> decompressionDurations;

    double compressionRatio() const
    {
        return (double)originalSize / compressedSize;
    }

    double bestCompressionSpeedMBps() const
    {
        if (compressionDurations.empty()) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        auto dur = *std::min_element(
                compressionDurations.begin(), compressionDurations.end());
        return (double)originalSize * 1000.0 / dur.count();
    }

    double bestDecompressionSpeedMBps() const
    {
        if (decompressionDurations.empty()) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        auto dur = *std::min_element(
                decompressionDurations.begin(), decompressionDurations.end());
        return (double)originalSize * 1000.0 / dur.count();
    }

    nlohmann::json json() const;
    static std::string header();
    std::string pretty() const;
};

std::vector<BenchmarkResult> benchmark(
        const tools::io::InputSet& inputs,
        const BenchmarkArgs& args);

} // namespace openzl::bench
