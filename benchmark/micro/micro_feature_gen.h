// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/common/stream.h"
#include "openzl/common/vector.h"
#include "openzl/compress/selectors/ml/features.h"
#include "openzl/zl_config.h"
#include "openzl/zl_version.h"

// Our tools don't really compile outside of fbcode atm, this is a hack to
// make sure we compile this benchmark only in fbcode env.
#if ZL_IS_FBCODE

#    include "benchmark/benchmark_testcase.h"
#    include "benchmark/micro/micro_bench.h"
#    include "tools/zstrong_ml.h"

namespace zstrong::bench::micro {

using zstrong::ml::FeatureMap;
using zstrong::ml::features::IntFeatureGenerator;

inline void registerFeatureGeneratorBench()
{
    auto benchFunc = [](std::shared_ptr<BenchmarkData> data,
                        benchmark::State& state) {
        IntFeatureGenerator generator;
        for (auto _ : state) {
            FeatureMap fmap;
            generator.getFeatures(
                    fmap,
                    data->data().data(),
                    ZL_Type_numeric,
                    data->width(),
                    data->size());
            benchmark::DoNotOptimize(fmap);
            benchmark::ClobberMemory();
        }
    };
    std::vector<std::shared_ptr<BenchmarkData>> data = {
        std::make_shared<UniformDistributionData<uint64_t>>(
                10240, std::nullopt),
        std::make_shared<UniformDistributionData<uint32_t>>(
                10240, std::nullopt),
        std::make_shared<UniformDistributionData<uint16_t>>(
                10240, std::nullopt),
        std::make_shared<UniformDistributionData<uint8_t>>(10240, std::nullopt),
    };
    for (auto& d : data) {
        MiscMicroBenchmarkTestcase(
                "IntFeatureGenerator", "generate", benchFunc, d)
                .registerBenchmarks();
    }
}

} // namespace zstrong::bench::micro

#else

namespace zstrong::bench::micro {
void inline registerFeatureGeneratorBench() {}
} // namespace zstrong::bench::micro

#endif // ZL_IS_FBCODE

const size_t kDefaultVectorCapacity = 1024;

inline VECTOR(LabeledFeature) empty_vector()
{
    // Wrap this function to work around a static analysis bug.
    return VECTOR_EMPTY(kDefaultVectorCapacity);
}

namespace zstrong::bench::micro {
template <typename Int>
void inline registerFeatureGenIntegerBench(size_t size)
{
    const auto benchFunc = [size = size](benchmark::State& state) {
        const std::vector<Int> data(size, 1);
        ZL_TypedRef* stream = ZL_TypedRef_createNumeric(
                data.data(), sizeof(Int), data.size());

        VECTOR(LabeledFeature) features = empty_vector();

        while (state.KeepRunning()) {
            ZL_Report report = FeatureGen_integer(stream, &features, nullptr);
            VECTOR_CLEAR(features);
            benchmark::DoNotOptimize(report);
            benchmark::ClobberMemory();
        }

        VECTOR_DESTROY(features);
        ZL_TypedRef_free(stream);
    };

    std::string typeName = std::to_string(sizeof(Int) * 8);
    RegisterBenchmark("FeatureGen_integer " + typeName, benchFunc);
}
} // namespace zstrong::bench::micro
