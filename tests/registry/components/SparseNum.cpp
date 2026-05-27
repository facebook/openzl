// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/SparseNum.hpp"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

#include <cstdint>

namespace openzl::tests::components {
namespace {

template <typename T>
std::vector<T> sparseVector(datagen::DataGen& gen, size_t maxElts, T min, T max)
{
    std::vector<T> values;
    size_t const numElts = gen.usize_range("numElts", 0, maxElts);
    values.reserve(numElts);
    for (size_t i = 0; i < numElts; ++i) {
        if (gen.coin("isZero", 0.75)) {
            values.push_back(0);
            continue;
        }
        T value = gen.randVal<T>("value", min, max);
        if (value == 0) {
            value = 1;
        }
        values.push_back(value);
    }
    return values;
}

class SparseNumComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "SparseNum";
    }

    int minFormatVersion() const override
    {
        return 25;
    }

    size_t compressBound(poly::span<const Input> inputs) const override
    {
        return 3 * this->OpenZLComponent::compressBound(inputs);
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::SparseNum{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(U8OpenZLInput::make(std::vector<uint8_t>{}));
        inputs.push_back(U8OpenZLInput::make(std::vector<uint8_t>{ 0, 0, 0 }));
        inputs.push_back(U8OpenZLInput::make(std::vector<uint8_t>{ 1, 2, 3 }));
        inputs.push_back(U8OpenZLInput::make(std::vector<uint8_t>{ 0, 0, 7 }));
        inputs.push_back(U8OpenZLInput::make(std::vector<uint8_t>{ 7, 0, 0 }));
        inputs.push_back(
                U8OpenZLInput::make(
                        std::vector<uint8_t>{ 0, 1, 0, 0, 2, 0, 3, 0 }));
        inputs.push_back(
                U16OpenZLInput::make(
                        std::vector<uint16_t>{ 0, 1024, 0, 0, 65535 }));
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{ 0, 0, 1, 0, UINT32_MAX, 0 }));
        inputs.push_back(
                U64OpenZLInput::make(
                        std::vector<uint64_t>{ 0, 1, 0, UINT64_MAX, 0, 0 }));
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
            auto widthChoice = gen.usize_range("width", 0, 3);
            switch (widthChoice) {
                case 0: {
                    auto maxElts = maxInputSize / sizeof(uint8_t);
                    inputs.push_back(
                            U8OpenZLInput::make(
                                    sparseVector<uint8_t>(
                                            gen, maxElts, 0, UINT8_MAX)));
                    break;
                }
                case 1: {
                    auto maxElts = maxInputSize / sizeof(uint16_t);
                    inputs.push_back(
                            U16OpenZLInput::make(
                                    sparseVector<uint16_t>(
                                            gen, maxElts, 0, UINT16_MAX)));
                    break;
                }
                case 2: {
                    auto maxElts = maxInputSize / sizeof(uint32_t);
                    inputs.push_back(
                            U32OpenZLInput::make(
                                    sparseVector<uint32_t>(
                                            gen, maxElts, 0, UINT32_MAX)));
                    break;
                }
                case 3: {
                    auto maxElts = maxInputSize / sizeof(uint64_t);
                    inputs.push_back(
                            U64OpenZLInput::make(
                                    sparseVector<uint64_t>(
                                            gen, maxElts, 0, UINT64_MAX)));
                    break;
                }
            }
        }
        return inputs;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makeSparseNumComponent()
{
    return std::make_unique<SparseNumComponent>();
}

} // namespace openzl::tests::components
