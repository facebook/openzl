// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <benchmark/benchmark.h>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>

#include "benchmark/benchmark_data.h"
#include "benchmark/benchmark_testcase.h"
#include "benchmark/e2e/e2e_bench.h"
#include "benchmark/e2e/e2e_compressor.h"
#include "benchmark/e2e/e2e_zstrong_utils.h"
#include "benchmark/unitBench/scenarios/sao_graph.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_public_nodes.h"

namespace zstrong::bench::e2e {
namespace sao {

using namespace zstrong::bench::utils;

namespace {

class SAOCompressor : public ZstrongCompressor {
   private:
    ZL_GraphID configureGraph(ZL_Compressor* cgraph) override
    {
        return sao_graph_v1(cgraph);
    }

   public:
    using ZstrongCompressor::ZstrongCompressor;

    virtual std::string name() override
    {
        return "SAO";
    }
};

class BlockSAOCompressor : public SAOCompressor {
   private:
    const size_t blockSize_ = 1008;
    std::vector<std::string_view> getBlocks(const std::string_view src)
    {
        std::vector<std::string_view> blocks;
        for (size_t i = 0; i < src.size(); i += blockSize_) {
            std::string_view block(
                    src.data() + i, std::min(blockSize_, src.size() - i));
            blocks.push_back(block);
        }
        return blocks;
    }

   public:
    using SAOCompressor::SAOCompressor;
    virtual std::string name() override
    {
        return fmt::format("BlocksSAO(blockSize={})", blockSize_);
    }

    virtual void benchCompression(
            benchmark::State& state,
            const std::string_view src) override
    {
        benchCompressions(state, getBlocks(src));
    }

    virtual void benchDecompression(
            benchmark::State& state,
            const std::string_view src) override
    {
        benchDecompressions(state, getBlocks(src));
    }
};

} // namespace

inline void registerSAOBenchmarks()
{
    try {
        E2EBenchmarkTestcase(
                std::make_shared<SAOCompressor>(),
                std::make_shared<FileData>("silesia/sao"))
                .registerBenchmarks();
        E2EBenchmarkTestcase(
                std::make_shared<BlockSAOCompressor>(),
                std::make_shared<FileData>("silesia/sao"))
                .registerBenchmarks();
    } catch (std::exception& e) {
        fprintf(stderr, "Error registering SAO benchmarks: %s\n", e.what());
    }
}
} // namespace sao
} // namespace zstrong::bench::e2e
