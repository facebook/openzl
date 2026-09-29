// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <numeric>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/openzl.hpp"
#include "tools/training/utils/benchmark.h"

namespace openzl {
namespace training {
namespace tests {
namespace {

std::vector<uint64_t> counterData(size_t numElts, uint64_t start)
{
    std::vector<uint64_t> data(numElts);
    std::iota(data.begin(), data.end(), start);
    return data;
}

size_t contentSize(poly::span<const Input> inputs)
{
    size_t size = 0;
    for (const auto& input : inputs) {
        size += input.contentSize();
    }
    return size;
}

void expectBenchmarked(const CompressionResult& result, size_t originalSize)
{
    EXPECT_EQ(result.originalSize, originalSize);
    EXPECT_GT(result.compressedSize, 0u);
    EXPECT_LT(result.compressedSize, originalSize);
    EXPECT_GT(result.compressionTime.count(), 0);
    EXPECT_GT(result.decompressionTime.count(), 0);
}

} // namespace

TEST(BenchmarkTest, SingleInputGraph)
{
    auto data0 = counterData(1000, 0);
    auto data1 = counterData(500, 1000);
    std::vector<Input> inputs;
    inputs.push_back(Input::refNumeric(poly::span<const uint64_t>(data0)));
    inputs.push_back(Input::refNumeric(poly::span<const uint64_t>(data1)));

    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    compressor.selectStartingGraph(ZL_GRAPH_COMPRESS_GENERIC);

    auto result = benchmark(compressor, inputs);
    ASSERT_TRUE(result.has_value());
    expectBenchmarked(*result, contentSize(inputs));

    // Each input is compressed independently, so the sizes are the sum of the
    // sizes of benchmarking each input on its own.
    auto result0 =
            benchmark(compressor, poly::span<const Input>(&inputs[0], 1));
    auto result1 =
            benchmark(compressor, poly::span<const Input>(&inputs[1], 1));
    ASSERT_TRUE(result0.has_value());
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(
            result->compressedSize,
            result0->compressedSize + result1->compressedSize);
}

TEST(BenchmarkTest, MultiInputGraph)
{
    const std::string data0(1000, 'a');
    const std::string data1(2000, 'b');
    MultiInput multiInput;
    multiInput.add(Input::refSerial(data0));
    multiInput.add(Input::refSerial(data1));

    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    compressor.selectStartingGraph(compressor.buildStaticGraph(
            ZL_NODE_CONCAT_SERIAL,
            { ZL_GRAPH_COMPRESS_GENERIC, ZL_GRAPH_COMPRESS_GENERIC }));

    auto result = benchmark(compressor, *multiInput);
    ASSERT_TRUE(result.has_value());
    expectBenchmarked(*result, data0.size() + data1.size());
}

TEST(BenchmarkTest, FormatVersionMustBeSet)
{
    const std::string data(1000, 'a');
    std::vector<Input> inputs;
    inputs.push_back(Input::refSerial(data));

    Compressor compressor;
    compressor.selectStartingGraph(ZL_GRAPH_COMPRESS_GENERIC);
    ASSERT_EQ(compressor.getParameter(CParam::FormatVersion), 0);

    EXPECT_FALSE(benchmark(compressor, inputs).has_value());

    // The compressor is otherwise valid: it benchmarks once the format
    // version is set.
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    EXPECT_TRUE(benchmark(compressor, inputs).has_value());
}

} // namespace tests
} // namespace training
} // namespace openzl
