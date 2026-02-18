// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Bitunpack.hpp"
#include "openzl/zl_reflection.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {
// TODO: This only tests a small subset of possible nodes due to the
// constraints on generating inputs that work for all possible bit-widths.
// To fix this, add 3 new components to test prime bit-widths of 2-, 4-, and
// 8-byte integers:
// - Bitunpack13Component
// - Bitunpack23Component
// - Bitunpack37Component
class BitunpackComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Bitunpack";
    }

    int minFormatVersion() const override
    {
        return 6;
    }

    // Bitunpack expands serial data: numBits=1 gives 32x expansion
    // (1 byte → 8 u32 elements = 32 bytes), so the default bound is
    // not sufficient.
    size_t compressBound(poly::span<const Input> inputs) const override
    {
        return 64 * this->OpenZLComponent::compressBound(inputs);
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return {
            nodes::Bitunpack{ 1 }.parameterize(compressor),
            nodes::Bitunpack{ 3 }.parameterize(compressor),
            nodes::Bitunpack{ 7 }.parameterize(compressor),
            nodes::Bitunpack{ 8 }.parameterize(compressor),
            nodes::Bitunpack{ 13 }.parameterize(compressor),
            nodes::Bitunpack{ 16 }.parameterize(compressor),
            nodes::Bitunpack{ 27 }.parameterize(compressor),
            nodes::Bitunpack{ 32 }.parameterize(compressor),
            nodes::Bitunpack{ 63 }.parameterize(compressor),
            nodes::Bitunpack{ 64 }.parameterize(compressor),
        };
    }

    std::vector<NodeID> generateNodes(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<NodeID> nodes;
        for (size_t i = 0; i < num; ++i) {
            auto width = gen.i32_range("num_bits", 1, 64);
            nodes.push_back(nodes::Bitunpack{ width }.parameterize(compressor));
        }
        return nodes;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        // Only empty inputs are guaranteed to work for every num bits
        inputs.push_back(SerialOpenZLInput::make(""));
        return inputs;
    }

    int getNumBits(const Compressor& compressor, GraphID graph) const
    {
        auto node   = ZL_Compressor_Graph_getHeadNode(compressor.get(), graph);
        auto params = ZL_Compressor_Node_getLocalParams(compressor.get(), node);
        if (params.intParams.nbIntParams != 1) {
            throw std::runtime_error("Failed to get width");
        }
        return params.intParams.intParams[0].paramValue;
    }

    std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSize,
            const Compressor& compressor,
            GraphID graph) const override
    {
        auto numBits = getNumBits(compressor, graph);
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            auto input = gen.randString("data", maxInputSize);
            // Truncate to the smallest number of bytes that fit inputElts.
            auto inputElts = input.size() * 8 / numBits;
            auto inputSize = (inputElts * numBits + 7) / 8;
            input.resize(inputSize);
            inputs.push_back(SerialOpenZLInput::make(std::move(input)));
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeBitunpackComponent()
{
    return std::make_unique<BitunpackComponent>();
}
} // namespace openzl::tests::components
