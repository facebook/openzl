// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/zl_partition.h"
#include "openzl/cpp/codecs/Partition.hpp"
#include "tests/datagen/distributions/ConstantDistribution.h"
#include "tests/datagen/distributions/LogUniformDistribution.h"
#include "tests/datagen/distributions/UniformDistribution.h"
#include "tests/datagen/structures/VectorProducer.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

class PartitionBitpackComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "PartitionBitpack";
    }

    int minFormatVersion() const override
    {
        return 24;
    }

    // Partition produces buckets + extra_bits, roughly 2-3x expansion.
    size_t compressBound(poly::span<const Input> inputs) const override
    {
        return 4 * this->OpenZLComponent::compressBound(inputs);
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        std::vector<GraphID> graphs;
        graphs.push_back(ZL_GRAPH_PARTITION_BITPACK);
        {
            LocalParams params;
            params.addIntParam(
                    ZL_GRAPH_PARTITION_BITPACK_OPTIMAL_PID,
                    ZL_TernaryParam_enable);
            graphs.push_back(compressor.parameterizeGraph(
                    ZL_GRAPH_PARTITION_BITPACK,
                    { .localParams = std::move(params) }));
        }
        {
            LocalParams params;
            params.addIntParam(
                    ZL_GRAPH_PARTITION_BITPACK_OPTIMAL_PID,
                    ZL_TernaryParam_disable);
            graphs.push_back(compressor.parameterizeGraph(
                    ZL_GRAPH_PARTITION_BITPACK,
                    { .localParams = std::move(params) }));
        }

        // Custom 8 partitions: 3-bit bucket IDs
        graphs.push_back(
                nodes::Partition{ 0, { 2, 2, 4, 8, 16, 32, 64, 65408 } }(
                        compressor, ZL_GRAPH_BITPACK, ZL_GRAPH_STORE));

        // QuantizeLengths preset: 44 partitions, 6-bit bucket IDs
        graphs.push_back(
                nodes::Partition{ ZL_PartitionParamsPreset_quantizeLengths }(
                        compressor, ZL_GRAPH_BITPACK, ZL_GRAPH_STORE));

        return graphs;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        // Empty input
        inputs.push_back(U16OpenZLInput::make(std::vector<uint16_t>{}));
        // Small input (< 10 elements, triggers store fallback)
        inputs.push_back(
                U16OpenZLInput::make(std::vector<uint16_t>{ 0, 1, 2 }));
        // Medium input spanning a range
        {
            std::vector<uint16_t> v;
            v.reserve(100);
            for (uint16_t i = 0; i < 100; ++i) {
                v.push_back(i * 100);
            }
            inputs.push_back(U16OpenZLInput::make(std::move(v)));
        }
        // Input with values across the full 16-bit range
        {
            std::vector<uint16_t> v;
            v.reserve(256);
            for (size_t i = 0; i < 256; ++i) {
                v.push_back((uint16_t)(i * 256));
            }
            inputs.push_back(U16OpenZLInput::make(std::move(v)));
        }
        // Input with clustered values
        {
            std::vector<uint16_t> v;
            v.reserve(100);
            for (size_t i = 0; i < 50; ++i) {
                v.push_back((uint16_t)(i % 10));
            }
            for (size_t i = 0; i < 50; ++i) {
                v.push_back((uint16_t)(60000 + i % 10));
            }
            inputs.push_back(U16OpenZLInput::make(std::move(v)));
        }
        return inputs;
    }

    std::unique_ptr<OpenZLInput> generateLogUniformInput(
            datagen::DataGen& gen,
            size_t inputSize) const
    {
        datagen::VectorProducer<uint16_t> dist(
                std::make_unique<datagen::LogUniformDistribution<uint16_t>>(
                        gen.getRandWrapper()),
                std::make_unique<datagen::ConstantDistribution<size_t>>(
                        inputSize));
        return NumericOpenZLInput<uint16_t>::make(dist("vector"));
    }

    std::unique_ptr<OpenZLInput> generateUniformInput(
            datagen::DataGen& gen,
            size_t inputSize) const
    {
        datagen::VectorProducer<uint16_t> dist(
                std::make_unique<datagen::UniformDistribution<uint16_t>>(
                        gen.getRandWrapper()),
                std::make_unique<datagen::ConstantDistribution<size_t>>(
                        inputSize));
        return NumericOpenZLInput<uint16_t>::make(dist("vector"));
    }

    std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSize,
            const Compressor&,
            GraphID) const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            auto numElts = gen.usize_range(
                    "num_elts", 0, maxInputSize / sizeof(uint16_t));
            if (gen.coin("log_uniform", 0.5)) {
                inputs.push_back(generateLogUniformInput(gen, numElts));
            } else {
                inputs.push_back(generateUniformInput(gen, numElts));
            }
        }
        return inputs;
    }

    std::vector<Benchmark> benchmarks(
            Compressor& compressor,
            datagen::DataGen& gen) const override
    {
        struct BenchmarkParams {
            size_t inputSize;
            size_t numInputs;
        };
        // Tuned so that each benchmark takes approximately the same amount
        // of compression time, so each scenario is approximately evenly
        // weighted.
        std::array<BenchmarkParams, 3> kParams = {
            BenchmarkParams{ 1000, 20 },
            BenchmarkParams{ 10000, 20 },
            BenchmarkParams{ 100000, 10 },
        };
        std::vector<Benchmark> benchmarks;
        benchmarks.reserve(kParams.size());
        for (auto param : kParams) {
            std::vector<std::unique_ptr<OpenZLInput>> inputs;
            inputs.reserve(param.numInputs);
            for (size_t i = 0; i < param.numInputs; ++i) {
                inputs.push_back(generateLogUniformInput(gen, param.inputSize));
            }
            benchmarks.push_back(
                    Benchmark{
                            .name = "InputSize:"
                                    + std::to_string(param.inputSize),
                            .graph  = ZL_GRAPH_PARTITION_BITPACK,
                            .inputs = std::move(inputs),
                    });
        }
        return benchmarks;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makePartitionBitpackComponent()
{
    return std::make_unique<PartitionBitpackComponent>();
}
} // namespace openzl::tests::components
