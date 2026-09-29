// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <benchmark/benchmark.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "benchmark/benchmark_config.h"
#include "benchmark/e2e/e2e_zstrong_utils.h"
#include "openzl/compress/cctx.h"
#include "openzl/compress/codec_output_cache.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_codec_output_cache.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "tests/datagen/random_producer/PRNGWrapper.h"
#include "tests/datagen/structures/VectorOfTokensProducer.h"

namespace zstrong::bench::e2e::codec_output_cache {

void registerBenchmarks();

namespace {

struct SelectorScenario {
    const char* categoryName;
    const char* name;
    const ZL_GraphID* successors;
    size_t nbSuccessors;
};

struct InputSize {
    const char* name;
    size_t nbElements;
};

enum class CacheMode {
    enabled,
    disabled,
};

const ZL_GraphID kRangePackZstdSuccessors[]   = { ZL_GRAPH_RANGE_PACK_ZSTD };
const ZL_GraphID kFieldLZSuccessors[]         = { ZL_GRAPH_FIELD_LZ };
const ZL_GraphID kTokenizeFieldLZSuccessors[] = {
    ZL_GRAPH_TOKENIZE_DELTA_FIELD_LZ
};
const ZL_GraphID kAllExpensiveSuccessors[] = {
    ZL_GRAPH_FIELD_LZ,
    ZL_GRAPH_RANGE_PACK_ZSTD,
    ZL_GRAPH_TOKENIZE_DELTA_FIELD_LZ,
};

const SelectorScenario kScenarios[] = {
    { "ReplayMechanics",
      "SingleCandidate_RangePackZstd",
      kRangePackZstdSuccessors,
      std::size(kRangePackZstdSuccessors) },
    { "ReplayMechanics",
      "SingleCandidate_FieldLZ",
      kFieldLZSuccessors,
      std::size(kFieldLZSuccessors) },
    { "ReplayMechanics",
      "SingleCandidate_TokenizeDeltaFieldLZ",
      kTokenizeFieldLZSuccessors,
      std::size(kTokenizeFieldLZSuccessors) },
    { "ApplicationShapedWorkload",
      "ThreeExpensiveCandidates",
      kAllExpensiveSuccessors,
      std::size(kAllExpensiveSuccessors) },
};

constexpr InputSize kInputSizes[] = {
    { "64KiB", 16 * 1024 },
    { "256KiB", 64 * 1024 },
    { "1MiB", 256 * 1024 },
};

struct InputDeleter {
    void operator()(ZL_TypedRef* input) const
    {
        ZL_TypedRef_free(input);
    }
};

using InputPtr = std::unique_ptr<ZL_TypedRef, InputDeleter>;

void generateInput(std::vector<uint32_t>& input)
{
    if (input.empty()) {
        return;
    }

    openzl::tests::datagen::VectorOfTokensParameters params{};
    params.numTokens = input.size();
    auto random      = std::make_shared<openzl::tests::datagen::PRNGWrapper>(
            std::make_shared<std::mt19937>(0xDEADBEEF));
    openzl::tests::datagen::VectorOfTokensProducer producer(
            std::move(random), params);
    auto generated = producer("CodecOutputCacheBenchmark");
    if (generated.width != sizeof(input[0])
        || generated.data.size() != input.size() * sizeof(input[0])) {
        throw std::runtime_error("Unexpected stack-trace input shape");
    }
    for (size_t index = 0; index < input.size(); ++index) {
        std::memcpy(
                &input[index],
                generated.data.data() + index * sizeof(input[index]),
                sizeof(input[index]));
    }
}

class SelectorRun {
   public:
    SelectorRun(
            CacheMode cacheMode,
            size_t inputElements,
            const SelectorScenario& scenario)
            : cacheMode_(cacheMode),
              compressor_(utils::createCGraph()),
              cctx_(utils::createCCTX()),
              input_(inputElements),
              inputRef_(nullptr)
    {
        generateInput(input_);
        inputRef_.reset(ZL_TypedRef_createNumeric(
                input_.data(), sizeof(input_[0]), input_.size()));
        if (inputRef_ == nullptr) {
            throw std::bad_alloc{};
        }

        const auto selectorResult = ZL_Compressor_buildBruteForceSelectorGraph(
                compressor_.get(), scenario.successors, scenario.nbSuccessors);
        if (ZL_RES_isError(selectorResult)) {
            throw std::runtime_error{
                std::string{ "Failed building brute-force graph, " }
                + ZL_E_str(ZL_RES_error(selectorResult))
            };
        }
        const ZL_GraphID selector = ZL_RES_value(selectorResult);
        utils::ZS2_unwrap(
                ZL_Compressor_selectStartingGraphID(
                        compressor_.get(), selector),
                "Failed selecting brute-force graph");
        utils::ZS2_unwrap(
                ZL_CCtx_refCompressor(cctx_.get(), compressor_.get()),
                "Failed attaching brute-force graph");
        utils::ZS2_unwrap(
                ZL_CCtx_setParameter(
                        cctx_.get(), ZL_CParam_stickyParameters, 1),
                "Failed enabling sticky parameters");
        CCTX_setTryGraphCacheStatsEnabled(cctx_.get(), true);
        const size_t cacheBudget = cacheMode == CacheMode::enabled
                ? CodecCache_getDefaultMaxBytes()
                : 0;
        utils::ZS2_unwrap(
                ZL_CCtx_setTryGraphCacheBudget(cctx_.get(), cacheBudget),
                "Failed configuring automatic tryGraph cache");
        output_.resize(ZL_compressBound(input_.size() * sizeof(input_[0])));
    }

    size_t compress()
    {
        const ZL_Report result = ZL_CCtx_compressTypedRef(
                cctx_.get(), output_.data(), output_.size(), inputRef_.get());
        return utils::ZS2_unwrap(result, "Brute-force compression failed");
    }

    const std::vector<uint8_t>& output() const
    {
        return output_;
    }

    CodecCache_Stats stats() const
    {
        if (cacheMode_ == CacheMode::disabled) {
            return {};
        }
        CodecCache_Stats stats{};
        CCTX_getLastChunkTryGraphCacheStats(&stats, cctx_.get());
        return stats;
    }

   private:
    CacheMode cacheMode_;
    utils::CGraph_unique compressor_;
    utils::CCTX_unique cctx_;
    std::vector<uint32_t> input_;
    InputPtr inputRef_;
    std::vector<uint8_t> output_;
};

void benchmarkSelector(
        benchmark::State& state,
        CacheMode cacheMode,
        InputSize inputSize,
        SelectorScenario scenario)
{
    try {
        SelectorRun measured(cacheMode, inputSize.nbElements, scenario);
        const CacheMode referenceMode = cacheMode == CacheMode::disabled
                ? CacheMode::enabled
                : CacheMode::disabled;
        SelectorRun reference(referenceMode, inputSize.nbElements, scenario);
        const size_t measuredSize  = measured.compress();
        const size_t referenceSize = reference.compress();
        const size_t inputBytes    = inputSize.nbElements * sizeof(uint32_t);
        if (measuredSize != referenceSize
            || !std::equal(
                    measured.output().begin(),
                    measured.output().begin() + measuredSize,
                    reference.output().begin(),
                    reference.output().begin() + referenceSize)) {
            state.SkipWithError("Cache modes produced different frames");
            return;
        }
        if (measuredSize >= inputBytes) {
            state.SkipWithError(
                    "Scenario did not select a graph that beats Store");
            return;
        }

        size_t compressedSize = 0;
        for (auto _ : state) {
            (void)_;
            compressedSize = measured.compress();
            benchmark::DoNotOptimize(compressedSize);
            benchmark::DoNotOptimize(measured.output().data());
            benchmark::ClobberMemory();
        }
        const CodecCache_Stats stats = measured.stats();
        state.SetBytesProcessed((int64_t)(inputBytes * state.iterations()));
        state.counters["CandidateGraphs"]  = (double)scenario.nbSuccessors;
        state.counters["Size"]             = (double)inputBytes;
        state.counters["CacheHits"]        = (double)stats.hits;
        state.counters["CacheInserts"]     = (double)stats.inserts;
        state.counters["CacheMisses"]      = (double)stats.misses;
        state.counters["CacheBytesStored"] = (double)stats.bytesStored;
        state.counters["CacheArenaBytes"]  = (double)stats.arenaBytes;
        state.counters["CacheMemoryRatio"] =
                (double)stats.arenaBytes / (double)inputBytes;
        state.counters["CompressedSize"] = (double)compressedSize;
        state.counters["CompressionRatio"] =
                (double)inputBytes / (double)compressedSize;
    } catch (const std::exception& error) {
        state.SkipWithError(error.what());
    }
}

} // namespace

void registerBenchmarks()
{
    for (const SelectorScenario& scenario : kScenarios) {
        for (const InputSize inputSize : kInputSizes) {
            const std::string namePrefix =
                    std::string("E2E / BruteForceSelector/CodecOutputCache/")
                    + scenario.categoryName + "/" + scenario.name + "/";
            const std::string nameSuffix =
                    std::string(" / StackTraceTokensUInt32/") + inputSize.name
                    + " / Compress";
            const std::string cacheEnabledName =
                    namePrefix + "CacheEnabled" + nameSuffix;
            RegisterBenchmark(cacheEnabledName, [=](benchmark::State& state) {
                benchmarkSelector(
                        state, CacheMode::enabled, inputSize, scenario);
            });
            const std::string cacheDisabledName =
                    namePrefix + "CacheDisabled" + nameSuffix;
            RegisterBenchmark(cacheDisabledName, [=](benchmark::State& state) {
                benchmarkSelector(
                        state, CacheMode::disabled, inputSize, scenario);
            });
        }
    }
}

} // namespace zstrong::bench::e2e::codec_output_cache
