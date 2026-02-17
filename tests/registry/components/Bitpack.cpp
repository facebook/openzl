// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Bitpack.hpp"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {
class BitpackComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Bitpack";
    }

    int minFormatVersion() const override
    {
        return 3;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        auto graph = graphs::Bitpack{}(compressor);
        return { graph };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 0, 1, 2, 3 }));
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{ 0, 255 }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 0, 0, 0 }));
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{ 42 }));
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{ 0 }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ UINT32_MAX }));
        inputs.push_back(
                U64OpenZLInput::make(
                        std::vector<uint64_t>{ 257, 100000, 0, 0, 0, 0 }));
        inputs.push_back(
                U64OpenZLInput::make(std::vector<uint64_t>{ UINT64_MAX }));
        inputs.push_back(
                U8OpenZLInput::make(std::vector<uint8_t>{ 0, 1, 127, 255 }));
        inputs.push_back(
                U16OpenZLInput::make(
                        std::vector<uint16_t>{ 0, 1, UINT16_MAX }));
        inputs.push_back(SerialOpenZLInput::make(""));
        inputs.push_back(SerialOpenZLInput::make("x"));
        inputs.push_back(SerialOpenZLInput::make(std::string(100, '\xAB')));
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
            auto typeChoice = gen.usize_range("type", 0, 4);
            switch (typeChoice) {
                case 0: {
                    auto maxElts = std::min(
                            maxInputSize / sizeof(uint8_t), size_t(131072));
                    auto maxVal = gen.u8_range("max_val", 0, UINT8_MAX);
                    inputs.push_back(
                            U8OpenZLInput::make(gen.randVector<uint8_t>(
                                    "values", 0, maxVal, maxElts)));
                    break;
                }
                case 1: {
                    auto maxElts = maxInputSize / sizeof(uint16_t);
                    auto maxVal  = gen.u16_range("max_val", 0, UINT16_MAX);
                    inputs.push_back(
                            U16OpenZLInput::make(gen.randVector<uint16_t>(
                                    "values", 0, maxVal, maxElts)));
                    break;
                }
                case 2: {
                    auto maxElts = maxInputSize / sizeof(uint32_t);
                    auto maxVal  = gen.u32_range("max_val", 0, UINT32_MAX);
                    inputs.push_back(
                            U32OpenZLInput::make(gen.randVector<uint32_t>(
                                    "values", 0, maxVal, maxElts)));
                    break;
                }
                case 3: {
                    auto maxElts = maxInputSize / sizeof(uint64_t);
                    auto maxVal  = gen.u64_range("max_val", 0, UINT64_MAX);
                    inputs.push_back(
                            U64OpenZLInput::make(gen.randVector<uint64_t>(
                                    "values", 0, maxVal, maxElts)));
                    break;
                }
                case 4: {
                    inputs.push_back(
                            SerialOpenZLInput::make(
                                    gen.randString("data", maxInputSize)));
                    break;
                }
            }
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeBitpackComponent()
{
    return std::make_unique<BitpackComponent>();
}
} // namespace openzl::tests::components
