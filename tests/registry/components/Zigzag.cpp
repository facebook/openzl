// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Zigzag.hpp"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {
class ZigzagComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Zigzag";
    }

    int minFormatVersion() const override
    {
        return 3;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::Zigzag{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{ 0 }));
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{ 1,
                                               static_cast<uint32_t>(-1),
                                               2,
                                               static_cast<uint32_t>(-2) }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 0, 0, 0 }));
        // Min/max values for u8
        inputs.push_back(
                U8OpenZLInput::make(
                        std::vector<uint8_t>{ 0, 1, 127, 128, 255 }));
        // Min/max values for u16
        inputs.push_back(
                U16OpenZLInput::make(
                        std::vector<uint16_t>{ 0, 1, 32767, 32768, 65535 }));
        // Min/max values for u32
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{
                                0, 1, 0x7FFFFFFF, 0x80000000, UINT32_MAX }));
        // Min/max values for u64
        inputs.push_back(
                U64OpenZLInput::make(
                        std::vector<uint64_t>{ 0,
                                               1,
                                               0x7FFFFFFFFFFFFFFF,
                                               0x8000000000000000,
                                               UINT64_MAX }));
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
                    auto maxElts = maxInputSize / sizeof(uint8_t);
                    inputs.push_back(
                            U8OpenZLInput::make(gen.randVector<uint8_t>(
                                    "values", 0, UINT8_MAX, maxElts)));
                    break;
                }
                case 1: {
                    auto maxElts = maxInputSize / sizeof(uint16_t);
                    inputs.push_back(
                            U16OpenZLInput::make(gen.randVector<uint16_t>(
                                    "values", 0, UINT16_MAX, maxElts)));
                    break;
                }
                case 2: {
                    auto maxElts = maxInputSize / sizeof(uint32_t);
                    inputs.push_back(
                            U32OpenZLInput::make(gen.randVector<uint32_t>(
                                    "values", 0, UINT32_MAX, maxElts)));
                    break;
                }
                case 3: {
                    auto maxElts = maxInputSize / sizeof(uint64_t);
                    inputs.push_back(
                            U64OpenZLInput::make(gen.randVector<uint64_t>(
                                    "values", 0, UINT64_MAX, maxElts)));
                    break;
                }
            }
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeZigzagComponent()
{
    return std::make_unique<ZigzagComponent>();
}
} // namespace openzl::tests::components
