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
    std::optional<size_t> blockSize;
};

struct BlockResult {
    size_t originalSize;
    size_t compressedSize;
    std::vector<std::chrono::nanoseconds> compressionDurations;
    std::vector<std::chrono::nanoseconds> decompressionDurations;

    std::chrono::nanoseconds bestCompressionDuration() const
    {
        if (compressionDurations.empty()) {
            return std::chrono::nanoseconds(0);
        }
        return *std::min_element(
                compressionDurations.begin(), compressionDurations.end());
    }

    std::chrono::nanoseconds bestDecompressionDuration() const
    {
        if (decompressionDurations.empty()) {
            return std::chrono::nanoseconds(0);
        }
        return *std::min_element(
                decompressionDurations.begin(), decompressionDurations.end());
    }

    nlohmann::json json() const;
};

struct BenchmarkResult {
    std::string fileName;
    std::string compressorName;
    nlohmann::json compressorConfig;
    std::vector<BlockResult> blockResults;

    size_t originalSize() const
    {
        size_t total = 0;
        for (const auto& block : blockResults) {
            total += block.originalSize;
        }
        return total;
    }

    size_t compressedSize() const
    {
        size_t total = 0;
        for (const auto& block : blockResults) {
            total += block.compressedSize;
        }
        return total;
    }

    double compressionRatio() const
    {
        return (double)originalSize() / compressedSize();
    }

    std::chrono::nanoseconds bestCompressionDuration() const
    {
        std::chrono::nanoseconds total{ 0 };
        for (const auto& block : blockResults) {
            total += block.bestCompressionDuration();
        }
        return total;
    }

    std::chrono::nanoseconds bestDecompressionDuration() const
    {
        std::chrono::nanoseconds total{ 0 };
        for (const auto& block : blockResults) {
            total += block.bestDecompressionDuration();
        }
        return total;
    }

    double bestCompressionSpeedMBps() const
    {
        auto dur = bestCompressionDuration();
        if (dur.count() == 0) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return (double)originalSize() * 1000.0 / dur.count();
    }

    double bestDecompressionSpeedMBps() const
    {
        auto dur = bestDecompressionDuration();
        if (dur.count() == 0) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return (double)originalSize() * 1000.0 / dur.count();
    }

    BenchmarkResult& operator+=(const BenchmarkResult& other)
    {
        if (!compressorName.empty() && compressorName != other.compressorName) {
            throw std::runtime_error(
                    "Cannot merge results from different compressors");
        }
        if (!compressorConfig.empty()
            && compressorConfig != other.compressorConfig) {
            throw std::runtime_error(
                    "Cannot merge results from different compressor configs");
        }

        fileName         = "total";
        compressorName   = other.compressorName;
        compressorConfig = other.compressorConfig;
        blockResults.insert(
                blockResults.end(),
                other.blockResults.begin(),
                other.blockResults.end());
        return *this;
    }

    BenchmarkResult operator+(const BenchmarkResult& other) const
    {
        auto result = *this;
        result += other;
        return result;
    }

    nlohmann::json json() const;
    static std::string header();
    std::string pretty() const;
};

std::vector<BenchmarkResult> benchmark(
        const tools::io::InputSet& inputs,
        const BenchmarkArgs& args);

} // namespace openzl::bench
