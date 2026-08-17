// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <stddef.h>
#include <chrono>

#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/Input.hpp"
#include "openzl/cpp/poly/Optional.hpp"
#include "openzl/cpp/poly/Span.hpp"
#include "tools/training/utils/utils.h"

namespace openzl {
namespace training {

struct CompressionResult {
    size_t originalSize{ 0 };
    size_t compressedSize{ 0 };
    std::chrono::nanoseconds compressionTime{ 0 };
    std::chrono::nanoseconds decompressionTime{ 0 };

    float compressionRatio() const
    {
        return (float)originalSize / compressedSize;
    }

    float compressionSpeedMBps() const
    {
        return ((float)originalSize * 1000.0) / compressionTime.count();
    }

    float decompressionSpeedMBps() const
    {
        return ((float)originalSize * 1000.0) / decompressionTime.count();
    }

    /// @returns The result as a fitness vector, in which smaller is better, as
    /// expected by dominates() and the genetic algorithm.
    std::vector<float> asFloatVector() const
    {
        return {
            (float)compressedSize,
            (float)compressionTime.count(),
            (float)decompressionTime.count(),
        };
    }

    bool operator<(const CompressionResult& other) const
    {
        return std::tie(compressedSize, compressionTime, decompressionTime)
                < std::tie(
                        other.compressedSize,
                        other.compressionTime,
                        other.decompressionTime);
    }

    bool operator==(const CompressionResult& other) const
    {
        return std::tie(compressedSize, compressionTime, decompressionTime)
                == std::tie(
                        other.compressedSize,
                        other.compressionTime,
                        other.decompressionTime);
    }

    bool dominates(const CompressionResult& other) const
    {
        if (*this == other) {
            return false;
        }
        return compressedSize <= other.compressedSize
                && compressionTime <= other.compressionTime
                && decompressionTime <= other.decompressionTime;
    }

    CompressionResult& operator+=(const CompressionResult& other)
    {
        originalSize += other.originalSize;
        compressedSize += other.compressedSize;
        compressionTime += other.compressionTime;
        decompressionTime += other.decompressionTime;
        return *this;
    }
};

/**
 * Benchmark @p compressor which may be a multi-input graph on each set of
 * inputs in @p inputs.
 *
 * @returns the compression results, or poly::nullopt on failure
 */
poly::optional<CompressionResult> benchmark(
        const Compressor& compressor,
        poly::span<const MultiInput> inputs);

/**
 * Benchmark @p compressor which must be a single-input graph across each input
 * in @p inputs.
 *
 * @returns the compression results, or poly::nullopt on failure
 *
 * @deprecated ACE uses this, but it needs to be migrated to the MultiInput
 * benchmark.
 */
poly::optional<CompressionResult> benchmark(
        const Compressor& compressor,
        poly::span<const Input> inputs);

} // namespace training
} // namespace openzl
