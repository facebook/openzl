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

// Cap u32 test values so most land in the linear histogram region, giving the
// partition optimizer enough density to find non-trivial partitions.
constexpr uint64_t kMaxGeneratedU32 = 1u << 23;

template <typename T>
T maxValue()
{
    if constexpr (std::is_same_v<T, uint32_t>) {
        return kMaxGeneratedU32;
    }
    return std::numeric_limits<T>::max();
}

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
                nodes::Partition{ 0,
                                  { 2,
                                    2,
                                    4,
                                    8,
                                    16,
                                    32,
                                    64,
                                    kMaxGeneratedU32 + 1 - 128 } }(
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

    template <typename T>
    std::unique_ptr<OpenZLInput> generateLogUniformInput(
            datagen::DataGen& gen,
            size_t inputSize) const
    {
        datagen::VectorProducer<T> dist(
                std::make_unique<datagen::LogUniformDistribution<T>>(
                        gen.getRandWrapper(), 1, maxValue<T>()),
                std::make_unique<datagen::ConstantDistribution<size_t>>(
                        inputSize));
        return NumericOpenZLInput<T>::make(dist("vector"));
    }

    template <typename T>
    std::unique_ptr<OpenZLInput> generateUniformInput(
            datagen::DataGen& gen,
            size_t inputSize) const
    {
        datagen::VectorProducer<T> dist(
                std::make_unique<datagen::UniformDistribution<T>>(
                        gen.getRandWrapper(), 1, maxValue<T>()),
                std::make_unique<datagen::ConstantDistribution<size_t>>(
                        inputSize));
        return NumericOpenZLInput<T>::make(dist("vector"));
    }

    template <typename T>
    std::unique_ptr<OpenZLInput> generateInput(
            datagen::DataGen& gen,
            size_t maxInputSize) const
    {
        auto inputSize =
                gen.usize_range("num_elts", 0, maxInputSize / sizeof(T));
        if (gen.coin("log_uniform", 0.5)) {
            return generateLogUniformInput<T>(gen, inputSize);
        } else {
            return generateUniformInput<T>(gen, inputSize);
        }
    }

    std::unique_ptr<OpenZLInput> generateInput(
            datagen::DataGen& gen,
            size_t maxInputSize,
            size_t eltWidth) const
    {
        if (eltWidth == 2) {
            return generateInput<uint16_t>(gen, maxInputSize);
        } else {
            assert(eltWidth == 4);
            return generateInput<uint32_t>(gen, maxInputSize);
        }
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
            auto eltWidth = gen.coin("elt_width_4", 0.5) ? 4 : 2;
            inputs.push_back(generateInput(gen, maxInputSize, eltWidth));
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
            size_t width;
        };
        // Tuned so that each benchmark takes approximately the same amount
        // of compression time, so each scenario is approximately evenly
        // weighted.
        std::array<BenchmarkParams, 6> kParams = {
            BenchmarkParams{ 1000, 20, 2 },   BenchmarkParams{ 10000, 20, 2 },
            BenchmarkParams{ 100000, 10, 2 }, BenchmarkParams{ 1000, 20, 4 },
            BenchmarkParams{ 10000, 20, 4 },  BenchmarkParams{ 100000, 10, 4 },
        };
        std::vector<Benchmark> benchmarks;
        benchmarks.reserve(kParams.size());
        for (auto param : kParams) {
            std::vector<std::unique_ptr<OpenZLInput>> inputs;
            inputs.reserve(param.numInputs);
            for (size_t i = 0; i < param.numInputs; ++i) {
                if (param.width == 2) {
                    inputs.push_back(
                            generateLogUniformInput<uint16_t>(
                                    gen, param.inputSize));
                } else {
                    assert(param.width == 4);
                    inputs.push_back(
                            generateLogUniformInput<uint32_t>(
                                    gen, param.inputSize));
                }
            }
            benchmarks.push_back(
                    Benchmark{
                            .name = "InputSize:"
                                    + std::to_string(param.inputSize)
                                    + "/EltWidth:"
                                    + std::to_string(param.width),
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
