// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <functional>

#include "benchmark/benchmark_data.h"
#include "benchmark/benchmark_testcase.h"
#include "benchmark/micro/micro_transform.h"

namespace zstrong::bench::micro {

/**
 * TransformMicroBenchmarkTestcase:
 * A Micro-benchmark testcases that measuress a ZstrongTransform in Mbps.
 * When registered it adds two benchmarks, one for ecncoding and another for
 * decoding.
 */
class TransformMicroBenchmarkTestcase : public BenchmarkTestcase {
   private:
    std::shared_ptr<ZstrongTransform> _transform;
    std::shared_ptr<BenchmarkData> _data;

   public:
    TransformMicroBenchmarkTestcase(
            std::shared_ptr<ZstrongTransform> transform,
            std::shared_ptr<BenchmarkData> data)
            : _transform(std::move(transform)), _data(std::move(data))
    {
    }
    void registerBenchmarks() override;
};

class MiscMicroBenchmarkTestcase : public BenchmarkTestcase {
   private:
    std::string _name;
    std::string _op;
    std::function<void(std::shared_ptr<BenchmarkData>, benchmark::State&)>
            _func;
    std::shared_ptr<BenchmarkData> _data;

   public:
    MiscMicroBenchmarkTestcase(
            std::string name,
            std::string op,
            std::function<
                    void(std::shared_ptr<BenchmarkData>, benchmark::State&)>
                    func,
            std::shared_ptr<BenchmarkData> data)
            : _name(std::move(name)),
              _op(std::move(op)),
              _func(std::move(func)),
              _data(std::move(data))
    {
    }
    void registerBenchmarks() override;
};

/**
 * registerMicroBenchmarks:
 * Generates and registers all micro benchmark cases.
 */
void registerMicroBenchmarks();
void registerPivCoHuffmanBenchmarks();

} // namespace zstrong::bench::micro
