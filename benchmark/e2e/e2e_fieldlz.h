// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <benchmark/benchmark.h>
#include <fmt/format.h>
#include <math.h>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "benchmark/benchmark_data.h"
#include "benchmark/benchmark_testcase.h"
#include "benchmark/e2e/e2e_bench.h"
#include "benchmark/e2e/e2e_compressor.h"
#include "benchmark/e2e/e2e_zstrong_utils.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_public_nodes.h"
#include "tests/datagen/random_producer/PRNGWrapper.h"
#include "tests/datagen/structures/VectorOfTokensProducer.h"

namespace zstrong::bench::e2e {
namespace fieldlz {

using namespace zstrong::bench::utils;

namespace {
// FieldLZCompressor implements a benchmarks compressor for FieldLZ.
// It's initialized with the following parameters:
// - eltWidth: width of each input element in bytes
// - clevel: compression level
// - dlevel: decompression level
// Unlike most other compressor graphs, FieldLz reacts differently to different
// compression and decompression levels, which is why we want to include them.
class FieldLzCompressor : public ZstrongCompressor {
   private:
    ZL_GraphID configureGraph(ZL_Compressor* cgraph) override
    {
        // Build the conversion and FieldLz graphs
        auto fieldlz  = ZL_Compressor_registerFieldLZGraph(cgraph);
        auto startGid = addConversionFromSerial(cgraph, fieldlz, eltWidth_);

        // Set additional graph parameters: compression level and
        // decompression level
        ZS2_unwrap(
                ZL_Compressor_setParameter(
                        cgraph, ZL_CParam_compressionLevel, clevel_),
                "Failed setting compression level");
        ZS2_unwrap(
                ZL_Compressor_setParameter(
                        cgraph, ZL_CParam_decompressionLevel, dlevel_),
                "Failed setting decompression level");

        return startGid;
    }
    size_t eltWidth_;
    int clevel_, dlevel_;

   public:
    FieldLzCompressor(size_t eltWidth, int clevel, int dlevel)
            : ZstrongCompressor(),
              eltWidth_(eltWidth),
              clevel_(clevel),
              dlevel_(dlevel)
    {
    }

    virtual std::string name() override
    {
        // Return a human-readable name for the compressor.
        // We encode all the important configurations here - specifically the
        // width we operate on and the level of compression/decompression.
        return fmt::format(
                "FieldLz{}(clvl={}, dlvl={})", eltWidth_ * 8, clevel_, dlevel_);
    }
};
} // namespace

// Register the FieldLz benchmarks, this allows us to specify pairs of
// compressors and corpora and register a different benchmark testcase of each.
// In practice, each registered benchmark testcase will register two benchmarks
// - one for compression and another for decompression.
inline void registerFieldLzBenchmarks()
{
    // Create instances of the benchmark corpora.
    // We want to benchmark multiple different distributions with
    // different integer sizes.
    openzl::tests::datagen::VectorOfTokensParameters params{};
    params.numTokens = 100000;
    auto rand        = std::make_shared<openzl::tests::datagen::PRNGWrapper>(
            std::make_shared<std::mt19937>(0xdeadbeef));
    openzl::tests::datagen::VectorOfTokensProducer producer(rand, params);
    std::vector<std::shared_ptr<BenchmarkData>> corpora = {
        // 10K 16bit values with cardinality of 100
        std::make_shared<UniformDistributionData<uint16_t>>(10240, 100),
        // 10K 16bit values taken out of a normal distribution with
        // mean=UINT16_MAX / 2 and stddev=1024
        std::make_shared<NormalDistributionData<uint16_t>>(
                UINT16_MAX / 2, 1024, 10240),
        // 10K 32bit values with cardinality of 100
        std::make_shared<NormalDistributionData<uint32_t>>(
                UINT32_MAX / 2, 1024, 10240),
        // 100K 32bit values with cardinality of 100
        std::make_shared<NormalDistributionData<uint32_t>>(
                UINT32_MAX / 2, 1024, 102400),
        // 100K 32bit values that look like stack traces
        std::make_shared<FixedWidthDataProducerData>(producer)
    };
    for (auto corpus : corpora) {
        // For each corpus we want to test 3 different compressors that
        // operate on the same element width as the corpus, but each uses
        // different compression levels and decompression levels.
        std::vector<std::shared_ptr<FieldLzCompressor>> compressors = {
            std::make_shared<FieldLzCompressor>(corpus->width(), 0, 0),
            std::make_shared<FieldLzCompressor>(corpus->width(), 1, 1),
            std::make_shared<FieldLzCompressor>(corpus->width(), 3, 1),
            std::make_shared<FieldLzCompressor>(corpus->width(), 3, 7),
            std::make_shared<FieldLzCompressor>(corpus->width(), 7, 7),
        };
        for (auto compressor : compressors) {
            // Register a test case for each compressor and corpus.
            E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
        }
    }
}
} // namespace fieldlz
} // namespace zstrong::bench::e2e
