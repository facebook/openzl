// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <benchmark/benchmark.h>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "benchmark/benchmark_config.h"
#include "benchmark/benchmark_data.h"
#include "benchmark/micro/micro_bench.h"
#include "benchmark/micro/micro_feature_gen.h"
#include "benchmark/micro/micro_float32_deconstruct.h"
#include "benchmark/micro/micro_prefix.h"
#include "benchmark/micro/micro_transposeSplit.h"
#include "benchmark/micro/micro_varint.h"
#include "openzl/shared/varint.h"

namespace zstrong::bench::micro {

void TransformMicroBenchmarkTestcase::registerBenchmarks()
{
    auto EncodeBM = [transform = _transform,
                     data      = _data](benchmark::State& state) mutable {
        transform->benchEncoding(state, data->data());
    };
    RegisterBenchmark(
            ("MCR / " + _transform->name() + " / " + _data->name()
             + " / Encode"),
            EncodeBM);

    auto DecodeBM = [transform = _transform,
                     data      = _data](benchmark::State& state) mutable {
        transform->benchDecoding(state, data->data());
    };
    RegisterBenchmark(
            ("MCR / " + _transform->name() + " / " + _data->name()
             + " / Decode"),
            DecodeBM);
}

void MiscMicroBenchmarkTestcase::registerBenchmarks()
{
    RegisterBenchmark(
            "MCR / " + _name + " / " + _op + " / " + _data->name(),
            [data = _data, func = _func](benchmark::State& state) {
                func(data, state);
                state.SetBytesProcessed(
                        (int64_t)data->data().size()
                        * (int64_t)state.iterations());
            });
}

static void registerFloatDeconstructMicroBenchmarks(size_t bufferSize)
{
    auto const float32 = std::make_shared<FloatDeconstructTransform<uint32_t>>(
            1,
            3,
            FLTDECON_float32_deconstruct_encode,
            FLTDECON_float32_deconstruct_decode,
            "Float32");

    auto const bfloat16 = std::make_shared<FloatDeconstructTransform<uint16_t>>(
            1,
            1,
            FLTDECON_bfloat16_deconstruct_encode,
            FLTDECON_bfloat16_deconstruct_decode,
            "BrainFloat16");

    auto const float16 = std::make_shared<FloatDeconstructTransform<uint16_t>>(
            1,
            2,
            FLTDECON_float16_deconstruct_encode,
            FLTDECON_float16_deconstruct_decode,
            "Float16");

    auto const uniform32 = std::make_shared<UniformDistributionData<uint32_t>>(
            bufferSize, 100);

    auto const uniform16 = std::make_shared<UniformDistributionData<uint16_t>>(
            bufferSize, 100);

    TransformMicroBenchmarkTestcase(float32, uniform32).registerBenchmarks();
    TransformMicroBenchmarkTestcase(float16, uniform16).registerBenchmarks();
    TransformMicroBenchmarkTestcase(bfloat16, uniform16).registerBenchmarks();
}

static void registerTransposeSplit4MicroBenchmarks()
{
    std::vector<std::shared_ptr<BenchmarkData>> corpora = {
        std::make_shared<FixedSizeData>(1024, 4),
        std::make_shared<FixedSizeData>(10 * 1024, 4),
        std::make_shared<FixedSizeData>(100 * 1024, 4),
    };
    auto const transposeSplit4 =
            std::make_shared<TransposeSplit4Transform<uint8_t>>(
                    ZS_splitTransposeEncode,
                    ZS_splitTransposeDecode,
                    fmt::format("MicroTransposeSplit4"));
    for (auto corpus : corpora) {
        TransformMicroBenchmarkTestcase(transposeSplit4, corpus)
                .registerBenchmarks();
    }
}

static void registerTransposeSplit8MicroBenchmarks()
{
    std::vector<std::shared_ptr<BenchmarkData>> corpora = {
        std::make_shared<FixedSizeData>(1024, 8),
        std::make_shared<FixedSizeData>(2 * 1024, 8),
        std::make_shared<FixedSizeData>(3 * 1024, 8),
        std::make_shared<FixedSizeData>(10 * 1024, 8),
        std::make_shared<FixedSizeData>(100 * 1024, 8),
    };
    auto const transposeSplit8 =
            std::make_shared<TransposeSplit8Transform<uint8_t>>(
                    ZS_splitTransposeEncode,
                    ZS_splitTransposeDecode,
                    fmt::format("MicroTransposeSplit8"));
    for (auto corpus : corpora) {
        TransformMicroBenchmarkTestcase(transposeSplit8, corpus)
                .registerBenchmarks();
    }
}

template <typename Int>
size_t varintEncodeFast(Int val, uint8_t* dst)
{
    if constexpr (sizeof(Int) == 4) {
        return ZL_varintEncode32Fast(val, dst);
    } else {
        return ZL_varintEncode64Fast(val, dst);
    }
}

template <typename Int>
void registerVarintBenchmark(std::shared_ptr<BenchmarkData> data)
{
    auto const varint = std::make_shared<
            VarintTransform<Int, varintEncodeFast<Int>, ZL_varintDecode>>(
            "Varint" + std::to_string(sizeof(Int) * 8));
    TransformMicroBenchmarkTestcase(varint, data).registerBenchmarks();
}

template <typename Int>
void registerVarintBenchmarks(size_t size)
{
    auto const oneByte = std::make_shared<UniformDistributionData<Int>>(
            size, std::nullopt, 0, 127);
    registerVarintBenchmark<Int>(oneByte);
    auto const oneOrTwoBytes = std::make_shared<UniformDistributionData<Int>>(
            size, std::nullopt, 0, 255);
    registerVarintBenchmark<Int>(oneOrTwoBytes);
    auto const twoOrThreeBytes = std::make_shared<UniformDistributionData<Int>>(
            size, std::nullopt, 0, 1 << 15);
    registerVarintBenchmark<Int>(twoOrThreeBytes);
    auto const fourOrFiveBytes = std::make_shared<UniformDistributionData<Int>>(
            size, std::nullopt, 0, 1u << 29);
    registerVarintBenchmark<Int>(fourOrFiveBytes);
    if constexpr (sizeof(Int) == 8) {
        auto const eightBytes = std::make_shared<UniformDistributionData<Int>>(
                size, std::nullopt, 0, (1ull << 56) - 1);
        registerVarintBenchmark<Int>(eightBytes);
        auto const eightOrNineBytes =
                std::make_shared<UniformDistributionData<Int>>(
                        size, std::nullopt, 0, 1ull << 57);
        registerVarintBenchmark<Int>(eightOrNineBytes);
    }
    auto const fullRange =
            std::make_shared<UniformDistributionData<Int>>(size, std::nullopt);
    registerVarintBenchmark<Int>(fullRange);
}

static void registerPrefixMicroBenchmarks()
{
    std::vector<std::shared_ptr<VariableSizeData>> corpora = {
        std::make_shared<VariableSizeData>(true, 1024, 1, 10, 4),
        std::make_shared<VariableSizeData>(true, 1024, 5, 15, 4),
        std::make_shared<VariableSizeData>(true, 1024, 10, 20, 4),
        std::make_shared<VariableSizeData>(true, 10 * 1024, 1, 10, 4),
        std::make_shared<VariableSizeData>(true, 10 * 1024, 5, 15, 4),
        std::make_shared<VariableSizeData>(true, 10 * 1024, 10, 20, 4),
        std::make_shared<VariableSizeData>(true, 100 * 1024, 1, 10, 4),
        std::make_shared<VariableSizeData>(true, 100 * 1024, 5, 15, 4),
        std::make_shared<VariableSizeData>(true, 100 * 1024, 10, 20, 4),
    };
    for (auto corpus : corpora) {
        auto const prefix = std::make_shared<PrefixTransform<uint8_t>>(
                ZS_encodePrefix,
                ZS_decodePrefix,
                "MicroPrefix",
                corpus->data().size(),
                corpus->getFieldSizes());
        TransformMicroBenchmarkTestcase(prefix, corpus).registerBenchmarks();
    }
}

void registerMicroBenchmarks()
{
    registerFloatDeconstructMicroBenchmarks(10 * 1024);
    registerFloatDeconstructMicroBenchmarks(10 * 1024 * 1024);
    registerVarintBenchmarks<uint32_t>(10 * 1024);
    registerVarintBenchmarks<uint64_t>(10 * 1024);
    registerPrefixMicroBenchmarks();
    registerTransposeSplit4MicroBenchmarks();
    registerTransposeSplit8MicroBenchmarks();
    registerFeatureGeneratorBench();
    registerFeatureGenIntegerBench<uint8_t>(1 << 20);
    registerFeatureGenIntegerBench<uint16_t>(1 << 20);
    registerFeatureGenIntegerBench<uint32_t>(1 << 20);
    registerFeatureGenIntegerBench<uint64_t>(1 << 20);
    registerPivCoHuffmanBenchmarks();
}

} // namespace zstrong::bench::micro
