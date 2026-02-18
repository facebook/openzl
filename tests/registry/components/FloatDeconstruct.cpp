// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cstring>

#include "openzl/cpp/codecs/FloatDeconstruct.hpp"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

static std::vector<uint32_t> floatsToU32(std::initializer_list<float> floats)
{
    std::vector<uint32_t> result;
    result.reserve(floats.size());
    for (float f : floats) {
        uint32_t u;
        std::memcpy(&u, &f, sizeof(u));
        result.push_back(u);
    }
    return result;
}

class Float32DeconstructComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Float32Deconstruct";
    }

    int minFormatVersion() const override
    {
        return 4;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::Float32Deconstruct{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
        inputs.push_back(
                U32OpenZLInput::make(
                        floatsToU32({ 0.0f, 1.0f, -1.0f, 3.14f })));
        inputs.push_back(
                U32OpenZLInput::make(floatsToU32(
                        { std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::denorm_min() })));
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
            auto maxElts = maxInputSize / sizeof(uint32_t);
            inputs.push_back(
                    U32OpenZLInput::make(gen.randVector<uint32_t>(
                            "values", 0, UINT32_MAX, maxElts)));
        }
        return inputs;
    }
};

class BFloat16DeconstructComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "BFloat16Deconstruct";
    }

    int minFormatVersion() const override
    {
        return 4;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::BFloat16Deconstruct{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(U16OpenZLInput::make(std::vector<uint16_t>{}));
        // bf16: 0.0=0x0000, 1.0=0x3F80, -1.0=0xBF80
        inputs.push_back(
                U16OpenZLInput::make(
                        std::vector<uint16_t>{ 0x0000, 0x3F80, 0xBF80 }));
        // bf16 special values: +inf, -inf, NaN, -0, denorm_min, largest normal
        inputs.push_back(
                U16OpenZLInput::make(
                        std::vector<uint16_t>{ 0x7F80,
                                               0xFF80,
                                               0x7FC0,
                                               0x8000,
                                               0x0001,
                                               0x7F7F }));
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
            inputs.push_back(
                    U16OpenZLInput::make(gen.randVector<uint16_t>(
                            "values", 0, UINT16_MAX, maxElts)));
        }
        return inputs;
    }
};

class Float16DeconstructComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Float16Deconstruct";
    }

    int minFormatVersion() const override
    {
        return 4;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::Float16Deconstruct{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(U16OpenZLInput::make(std::vector<uint16_t>{}));
        // fp16: 0.0=0x0000, 1.0=0x3C00, -1.0=0xBC00
        inputs.push_back(
                U16OpenZLInput::make(
                        std::vector<uint16_t>{ 0x0000, 0x3C00, 0xBC00 }));
        // fp16 special values: +inf, -inf, NaN, -0, denorm_min, largest normal
        inputs.push_back(
                U16OpenZLInput::make(
                        std::vector<uint16_t>{ 0x7C00,
                                               0xFC00,
                                               0x7E00,
                                               0x8000,
                                               0x0001,
                                               0x7BFF }));
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
            inputs.push_back(
                    U16OpenZLInput::make(gen.randVector<uint16_t>(
                            "values", 0, UINT16_MAX, maxElts)));
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeFloat32DeconstructComponent()
{
    return std::make_unique<Float32DeconstructComponent>();
}
std::unique_ptr<OpenZLComponent> makeBFloat16DeconstructComponent()
{
    return std::make_unique<BFloat16DeconstructComponent>();
}
std::unique_ptr<OpenZLComponent> makeFloat16DeconstructComponent()
{
    return std::make_unique<Float16DeconstructComponent>();
}
} // namespace openzl::tests::components
