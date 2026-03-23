// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <limits.h>
#include <algorithm>

#include "openzl/codecs/partition/encode_partition_binding.h"
#include "openzl/codecs/zl_store.h"
#include "openzl/cpp/codecs/Partition.hpp"
#include "openzl/shared/bits.h"
#include "openzl/zl_reflection.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

uint64_t getMaxValue(const ZL_PartitionParams& params)
{
    uint64_t maxValue = params.startValue;
    for (size_t i = 0; i < params.numPartitions; ++i) {
        maxValue += params.partitionSizes[i];
    }
    return maxValue - 1;
}

size_t getSmallestWidthForValue(uint64_t value)
{
    if (value <= UCHAR_MAX) {
        return 1;
    }
    if (value <= USHRT_MAX) {
        return 2;
    }
    if (value <= UINT_MAX) {
        return 4;
    }
    return 8;
}

/// Inclusive maximum value for partition values given an element width.
uint64_t maxValueForWidth(size_t width)
{
    if (width >= 8) {
        return UINT64_MAX;
    }
    return (uint64_t(1) << (width * 8)) - 1;
}

/// Generate a random custom partition node for values in [0, maxValue].
/// When pow2Only is true, all partition sizes are powers of 2, which exercises
/// the compact pow2 header encoding path. Otherwise, arbitrary sizes are used,
/// which exercises the varint header encoding path.
nodes::Partition generateCustomPartition(
        datagen::DataGen& gen,
        uint64_t maxValue)
{
    const uint64_t startValue  = gen.boolean("start_at_zero")
             ? 0
             : gen.u64_range("start_value", 0, maxValue - 1);
    const bool endAtMaxValue   = gen.boolean("end_at_max_value");
    const bool powerOfTwoSizes = gen.boolean("power_of_two_sizes");

    std::vector<uint64_t> partitionSizes;
    uint64_t currentValue = startValue;
    while (currentValue < maxValue
           && partitionSizes.size() < ZL_PARTITION_MAX_PARTITIONS) {
        size_t size =
                gen.u64_range("partition_size", 1, maxValue - currentValue);
        if (powerOfTwoSizes) {
            size = 1ull << ZL_highbit64(size);
        }
        partitionSizes.push_back(size);
        currentValue += size;
    }
    if (endAtMaxValue && currentValue < maxValue) {
        if (partitionSizes.size() == ZL_PARTITION_MAX_PARTITIONS) {
            partitionSizes.back() += maxValue - currentValue;
        } else {
            partitionSizes.push_back(maxValue - currentValue);
        }
    }

    return nodes::Partition{ startValue, std::move(partitionSizes) };
}

class PartitionComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Partition";
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
        return {
            // All 4 presets
            nodes::Partition{ ZL_PartitionParamsPreset_quantizeOffsets }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
            nodes::Partition{ ZL_PartitionParamsPreset_quantizeLengths }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
            nodes::Partition{ ZL_PartitionParamsPreset_varbyte16 }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
            // Custom: 8-bit range [0, 255]
            nodes::Partition{ 0, { 16, 112, 128 } }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
            // Custom: 16-bit range with many small buckets
            nodes::Partition{ 0,
                              { 1,
                                1,
                                2,
                                4,
                                8,
                                16,
                                32,
                                64,
                                128,
                                256,
                                512,
                                1024,
                                2048,
                                61440 } }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
            // Custom: 32-bit range
            nodes::Partition{ 0, { 256, 65280, 4294901760 } }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
        };
    }

    std::vector<NodeID> generateNodes(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<NodeID> result;
        result.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            auto useCustom = gen.boolean("useCustom");
            if (useCustom) {
                auto widthChoice = gen.usize_range("width", 0, 3);
                size_t width     = widthChoice == 0 ? 1
                            : widthChoice == 1      ? 2
                            : widthChoice == 2      ? 4
                                                    : 8;
                auto maxValue    = maxValueForWidth(width);
                auto partition   = generateCustomPartition(gen, maxValue);
                result.push_back(partition.parameterize(compressor));
            } else {
                auto presetId = (ZL_PartitionParamsPreset)gen.i32_range(
                        "preset", 0, ZL_PartitionParamsPreset_custom - 1);
                result.push_back(
                        nodes::Partition{ presetId }.parameterize(compressor));
            }
        }
        return result;
    }

    std::vector<GraphID> generateGraphs(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<GraphID> result;
        result.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            auto useCustom = gen.boolean("useCustom");
            if (useCustom) {
                auto widthChoice = gen.usize_range("width", 0, 3);
                size_t width     = widthChoice == 0 ? 1
                            : widthChoice == 1      ? 2
                            : widthChoice == 2      ? 4
                                                    : 8;
                auto maxValue    = maxValueForWidth(width);
                auto partition   = generateCustomPartition(gen, maxValue);
                result.push_back(
                        partition(compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE));
            } else {
                auto presetId = (ZL_PartitionParamsPreset)gen.i32_range(
                        "preset", 0, ZL_PartitionParamsPreset_custom - 1);
                result.push_back(
                        nodes::Partition{ presetId }(
                                compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE));
            }
        }
        return result;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        // Empty inputs are valid for any partition configuration.
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(U8OpenZLInput::make(std::vector<uint8_t>{}));
        inputs.push_back(U16OpenZLInput::make(std::vector<uint16_t>{}));
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
        inputs.push_back(U64OpenZLInput::make(std::vector<uint64_t>{}));
        return inputs;
    }

    /// Extract partition parameters from a graph via the reflection API.
    ZL_PartitionParams getPartitionParams(
            const Compressor& compressor,
            GraphID graphID) const
    {
        auto node = ZL_Compressor_Graph_getHeadNode(compressor.get(), graphID);
        auto localParams =
                ZL_Compressor_Node_getLocalParams(compressor.get(), node);
        ZL_PartitionParams params;
        compressor.unwrap(
                ZL_PartitionParams_fromLocalParams(&params, &localParams));
        return params;
    }

    template <typename T>
    std::unique_ptr<OpenZLInput> generateInput(
            datagen::DataGen& gen,
            size_t maxInputSize,
            uint64_t lo,
            uint64_t hi) const
    {
        auto maxElts   = maxInputSize / sizeof(T);
        auto clampedLo = static_cast<T>(lo);
        auto clampedHi = static_cast<T>(
                std::min(hi, (uint64_t)std::numeric_limits<T>::max()));
        auto vec = gen.randVector<T>("values", clampedLo, clampedHi, maxElts);
        if (vec.empty()) {
            vec.push_back(clampedLo);
        }
        return NumericOpenZLInput<T>::make(std::move(vec));
    }

    std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSize,
            const Compressor& compressor,
            GraphID graphID) const override
    {
        auto params         = getPartitionParams(compressor, graphID);
        uint64_t lo         = params.startValue;
        uint64_t hi         = getMaxValue(params);
        size_t naturalWidth = getSmallestWidthForValue(hi);

        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            // Pick a width >= naturalWidth (fallthrough finds the first valid)
            auto widthChoice = gen.usize_range("width", 0, 3);
            switch (widthChoice) {
                case 0:
                    if (naturalWidth <= 1) {
                        inputs.push_back(
                                generateInput<uint8_t>(
                                        gen, maxInputSize, lo, hi));
                        break;
                    }
                    ZL_FALLTHROUGH;
                case 1:
                    if (naturalWidth <= 2) {
                        inputs.push_back(
                                generateInput<uint16_t>(
                                        gen, maxInputSize, lo, hi));
                        break;
                    }
                    ZL_FALLTHROUGH;
                case 2:
                    if (naturalWidth <= 4) {
                        inputs.push_back(
                                generateInput<uint32_t>(
                                        gen, maxInputSize, lo, hi));
                        break;
                    }
                    ZL_FALLTHROUGH;
                case 3:
                    inputs.push_back(
                            generateInput<uint64_t>(gen, maxInputSize, lo, hi));
                    break;
            }
        }
        return inputs;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makePartitionComponent()
{
    return std::make_unique<PartitionComponent>();
}
} // namespace openzl::tests::components
