// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tests/serialization/GraphBuilderUtils.h"

#include <algorithm>
#include <random>

#include "openzl/zl_reflection.h"

namespace openzl::tests {

size_t getCompressedBound(const std::unique_ptr<OpenZLInput>& input)
{
    size_t totalSrcSize = 0;
    for (const auto& content : input->inputs()) {
        totalSrcSize += content.contentSize();
        if (content.type() == openzl::Type::String) {
            totalSrcSize += content.numElts() * sizeof(*content.stringLens());
        }
    }
    totalSrcSize += input->inputs().size() * 256;
    return ZL_compressBound(totalSrcSize);
}

std::vector<uint32_t> getPartitionedStringLengths(
        const std::string& input,
        int numSegments,
        datagen::DataGen& gen)
{
    if (input.size() == 0) {
        return {};
    }
    numSegments = std::min<size_t>(numSegments, input.size());
    if (numSegments == 1) {
        return { (uint32_t)input.size() };
    }
    std::vector<uint32_t> partitions;
    partitions.reserve(input.size());
    for (size_t i = 1; i < input.size(); i++) {
        partitions.push_back(i);
    }
    std::mt19937 rng(gen.u32("shuffle"));
    std::shuffle(partitions.begin(), partitions.end(), rng);
    std::sort(partitions.begin(), partitions.begin() + numSegments - 1);
    std::vector<uint32_t> partitionLengths(numSegments);
    partitionLengths[0] = partitions[0];
    for (size_t i = 1; i < (size_t)(numSegments - 1); i++) {
        partitionLengths[i] = partitions[i] - partitions[i - 1];
    }
    partitionLengths[numSegments - 1] =
            input.size() - partitions[numSegments - 2];
    return partitionLengths;
}

std::unique_ptr<OpenZLInput> generateInputCompatibleWithGraph(
        Compressor& compressor,
        datagen::DataGen& gen,
        ZL_GraphID startingGraph)
{
    std::vector<std::unique_ptr<OpenZLInput>> openzlInputs;
    auto numInputs =
            ZL_Compressor_Graph_getNumInputs(compressor.get(), startingGraph);
    // Generate inputs of the correct type for non variable inputs
    for (size_t i = 0; i < numInputs; i++) {
        std::unique_ptr<OpenZLInput> randomInput;
        std::vector<ZL_Type> choices;
        auto mask = ZL_Compressor_Graph_getInputMask(
                compressor.get(), startingGraph, i);
        for (size_t shift = 0; shift < 4; shift++) {
            auto type = (ZL_Type)(1 << shift);
            if (mask & type) {
                choices.push_back(type);
            }
        }
        auto type = gen.choices("type", choices);
        switch (type) {
            case ZL_Type_serial: {
                auto inputStr = gen.randString("input", 4096);
                randomInput   = SerialOpenZLInput::make(inputStr);
                break;
            }
            case ZL_Type_numeric: {
                auto width =
                        gen.choices("width", (std::vector<int>){ 1, 2, 4, 8 });
                auto inputStr =
                        gen.randStringWithQuantizedLength("input", 4096, width);
                randomInput = makeNumericInput(inputStr, width);
                break;
            }
            case ZL_Type_struct: {
                auto width =
                        gen.choices("width", (std::vector<int>){ 1, 2, 4, 8 });
                auto inputStr =
                        gen.randStringWithQuantizedLength("input", 4096, width);
                randomInput = StructOpenZLInput::make(inputStr, width);
                break;
            }
            case ZL_Type_string: {
                auto inputStr = gen.randString("input", 4096);
                randomInput   = StringOpenZLInput::make(
                        inputStr,
                        getPartitionedStringLengths(
                                inputStr,
                                gen.i32_range("num_segments", 1, 50),
                                gen));
                break;
            }
            default:
                throw std::runtime_error("Unexpected type!");
        }
        if (numInputs == 1) {
            return randomInput;
        } else {
            openzlInputs.push_back(std::move(randomInput));
        }
    }
    return MultiOpenZLInput::make(std::move(openzlInputs));
}

} // namespace openzl::tests
