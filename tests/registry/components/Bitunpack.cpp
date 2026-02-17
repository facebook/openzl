// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Bitunpack.hpp"
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
        size_t totalSrcSize = 0;
        for (const auto& input : inputs) {
            totalSrcSize += input.contentSize();
        }
        return ZL_compressBound(32 * totalSrcSize);
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return {
            nodes::Bitunpack{ 1 }.parameterize(compressor),
            nodes::Bitunpack{ 2 }.parameterize(compressor),
            nodes::Bitunpack{ 3 }.parameterize(compressor),
            nodes::Bitunpack{ 4 }.parameterize(compressor),
            nodes::Bitunpack{ 5 }.parameterize(compressor),
            nodes::Bitunpack{ 6 }.parameterize(compressor),
            nodes::Bitunpack{ 7 }.parameterize(compressor),
            nodes::Bitunpack{ 8 }.parameterize(compressor),
            nodes::Bitunpack{ 16 }.parameterize(compressor),
            nodes::Bitunpack{ 32 }.parameterize(compressor),
            nodes::Bitunpack{ 64 }.parameterize(compressor),
        };
    }

    std::vector<NodeID> generateNodes(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        // All testable nodes are predefined, so no need to generate
        return {};
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(SerialOpenZLInput::make(""));
        // Inputs must be >= 8 bytes to work with numBits up to 64
        inputs.push_back(SerialOpenZLInput::make(std::string(8, '\xAA')));
        inputs.push_back(SerialOpenZLInput::make(std::string(16, '\x55')));
        inputs.push_back(SerialOpenZLInput::make(std::string(8, '\x00')));
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
            inputs.push_back(
                    SerialOpenZLInput::make(gen.randStringWithQuantizedLength(
                            "data", maxInputSize, 8)));
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
