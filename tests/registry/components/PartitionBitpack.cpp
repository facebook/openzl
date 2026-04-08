// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/zl_partition.h"
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
            auto maxElts = maxInputSize / sizeof(uint16_t);
            auto minVal  = gen.u16_range("min_val", 0, UINT16_MAX);
            auto maxVal  = gen.u16_range("max_val", minVal, UINT16_MAX);
            inputs.push_back(
                    U16OpenZLInput::make(gen.randVector<uint16_t>(
                            "values", minVal, maxVal, maxElts)));
        }
        return inputs;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makePartitionBitpackComponent()
{
    return std::make_unique<PartitionBitpackComponent>();
}
} // namespace openzl::tests::components
