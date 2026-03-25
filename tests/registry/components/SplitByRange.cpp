// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Split.hpp"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

class SplitByRangeComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "SplitByRange";
    }

    int minFormatVersion() const override
    {
        return 24;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::SplitByRange{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;

        // Empty input
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
        // Single element
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{ 42 }));
        // All same
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 5, 5, 5, 5, 5 }));
        // All zeros
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 0, 0, 0 }));
        // Two well-separated ranges
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{
                                100, 105, 110, 102, 5, 8, 3, 7 }));
        // Three ranges
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{
                                100, 110, 105, 5, 3, 8, 200, 210, 205 }));
        // Adjacent ranges (touching but non-overlapping)
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{ 0, 50, 99, 100, 150, 199 }));
        // Max value transitions
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{
                                UINT32_MAX, UINT32_MAX - 1, 0, 1 }));
        // Single range (no split)
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{ 50, 60, 70, 55, 65 }));
        // u8 inputs
        inputs.push_back(
                U8OpenZLInput::make(
                        std::vector<uint8_t>{ 200, 210, 220, 10, 20, 30 }));
        // u16 inputs
        inputs.push_back(
                U16OpenZLInput::make(
                        std::vector<uint16_t>{ 1000, 1100, 0, 50 }));
        // u64 inputs
        inputs.push_back(
                U64OpenZLInput::make(
                        std::vector<uint64_t>{ 1000000, 1000100, 0, 50 }));

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
            auto width =
                    gen.choices("width", std::vector<size_t>{ 1, 2, 4, 8 });
            auto data = gen.randStringWithQuantizedLength(
                    "data", maxInputSize, width);
            inputs.push_back(makeNumericInput(data, width));
        }
        return inputs;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makeSplitByRangeComponent()
{
    return std::make_unique<SplitByRangeComponent>();
}

} // namespace openzl::tests::components
