// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/DivideBy.hpp"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {
class DivideByComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "DivideBy";
    }

    int minFormatVersion() const override
    {
        return 16;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return {
            nodes::DivideBy{}.parameterize(compressor),
            nodes::DivideBy{ 1 }.parameterize(compressor),
            nodes::DivideBy{ 2 }.parameterize(compressor),
            nodes::DivideBy{ 10 }.parameterize(compressor),
        };
    }

    std::vector<NodeID> generateNodes(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        // Divisors whose LCM is 60, so inputs that are multiples of 60
        // will work with all generated nodes.
        constexpr uint64_t divisors[] = { 1,  2,  3,  4,  5,  6,
                                          10, 12, 15, 20, 30, 60 };
        std::vector<NodeID> result;
        result.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            auto idx = gen.usize_range("divisor_idx", 0, 11);
            result.push_back(
                    nodes::DivideBy{ divisors[idx] }.parameterize(compressor));
        }
        return result;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
        // All values must be divisible by all generated divisors
        // Generated divisors are from {1,2,3,4,5,6,10,12,15,20,30,60}
        // with LCM = 60, so values must be multiples of 60
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{ 60, 120, 180, 240 }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 60, 120, 180 }));
        // All zeros (divisible by anything)
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 0, 0, 0 }));
        // Mix of zero and non-zero
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 0, 60, 120 }));
        // u8 inputs (multiples of 60; 60 and 120 fit in u8)
        inputs.push_back(
                U8OpenZLInput::make(std::vector<uint8_t>{ 0, 60, 120 }));
        // u16 inputs
        inputs.push_back(
                U16OpenZLInput::make(std::vector<uint16_t>{ 0, 60, 120, 600 }));
        // u64 inputs
        inputs.push_back(
                U64OpenZLInput::make(
                        std::vector<uint64_t>{ 0, 60, 120, 6000 }));
        return inputs;
    }

    std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSize) const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            auto widthChoice = gen.usize_range("width", 0, 3);
            switch (widthChoice) {
                case 0: {
                    auto maxElts = std::min(
                            maxInputSize / sizeof(uint8_t), size_t(131072));
                    // Multiples of 60 that fit in u8: 0, 60, 120, 180, 240
                    auto vec = gen.randVector<uint8_t>("values", 0, 4, maxElts);
                    for (auto& v : vec) {
                        v *= 60;
                    }
                    inputs.push_back(U8OpenZLInput::make(std::move(vec)));
                    break;
                }
                case 1: {
                    auto maxElts = maxInputSize / sizeof(uint16_t);
                    auto vec     = gen.randVector<uint16_t>(
                            "values", 0, UINT16_MAX / 60, maxElts);
                    for (auto& v : vec) {
                        v *= 60;
                    }
                    inputs.push_back(U16OpenZLInput::make(std::move(vec)));
                    break;
                }
                case 2: {
                    auto maxElts = maxInputSize / sizeof(uint32_t);
                    auto vec     = gen.randVector<uint32_t>(
                            "values", 0, UINT32_MAX / 60, maxElts);
                    for (auto& v : vec) {
                        v *= 60;
                    }
                    inputs.push_back(U32OpenZLInput::make(std::move(vec)));
                    break;
                }
                case 3: {
                    auto maxElts = maxInputSize / sizeof(uint64_t);
                    auto vec     = gen.randVector<uint64_t>(
                            "values", 0, UINT64_MAX / 60, maxElts);
                    for (auto& v : vec) {
                        v *= 60;
                    }
                    inputs.push_back(U64OpenZLInput::make(std::move(vec)));
                    break;
                }
            }
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeDivideByComponent()
{
    return std::make_unique<DivideByComponent>();
}
} // namespace openzl::tests::components
