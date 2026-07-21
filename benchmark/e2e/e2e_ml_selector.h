// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "benchmark/benchmark_data.h"
#include "benchmark/e2e/e2e_bench.h"
#include "benchmark/e2e/e2e_compressor.h"
#include "benchmark/e2e/e2e_zstrong_utils.h"
#include "openzl/compress/selectors/ml/ml_selector_graph.h"

#include "tests/ml_selector_utils.h"

namespace zstrong::bench::e2e {
namespace ml_selector {

namespace {

// Runs the ML selector graph over constant-delta data, which the model always
// routes to the same successor - STORE. Measures the selector's per-compression
// overhead.
class MLSelectorCompressor : public ZstrongCompressor {
   private:
    openzl::tests::SampleBinaryGBTModel sampleModel_;
    GBTModel gbtModel_ = sampleModel_.getModel();

    ZL_GraphID configureGraph(ZL_Compressor* cgraph) override
    {
        const ZL_MLSelectorConfig config = {
            .model         = ZL_GBT,
            .runtimeConfig = &gbtModel_,
        };
        const std::vector<ZL_GraphID> successors = { ZL_GRAPH_STORE,
                                                     ZL_GRAPH_STORE };
        const auto graph                         = ZL_MLSelector_registerGraph(
                cgraph, &config, successors.data(), successors.size());
        if (ZL_RES_isError(graph)) {
            throw std::runtime_error("Failed to register ML selector graph");
        }
        return addConversionFromSerial(
                cgraph, ZL_RES_value(graph), sizeof(uint64_t));
    }

   public:
    using ZstrongCompressor::ZstrongCompressor;

    std::string name() override
    {
        return "MLSelector";
    }
};

} // namespace

inline void registerMLSelectorBenchmarks()
{
    try {
        auto compressor = std::make_shared<MLSelectorCompressor>();
        auto corpus     = std::make_shared<CustomDistributionData<uint64_t>>(
                1000, [](size_t size, size_t /* seed */) {
                    return openzl::tests::generateDeltaData(size);
                });
        E2EBenchmarkTestcase(compressor, corpus).registerBenchmarks();
    } catch (std::exception& e) {
        fprintf(stderr,
                "Error registering ML selector benchmarks: %s\n",
                e.what());
    }
}

} // namespace ml_selector
} // namespace zstrong::bench::e2e
