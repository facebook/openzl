// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <limits>

#include "openzl/codecs/bitSplit/encode_bitSplit_binding.h"
#include "openzl/cpp/codecs/BitsplitTop8.hpp"
#include "openzl/zl_reflection.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

class BitSplitComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "BitSplit";
    }

    int minFormatVersion() const override
    {
        return 24;
    }

    size_t compressBound(poly::span<const Input> inputs) const override
    {
        auto bound = this->OpenZLComponent::compressBound(inputs);
        return bound * 64;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        std::vector<NodeID> nodes;
        auto registerNode = [&](const std::vector<uint8_t>& widths) {
            auto node = compressor.unwrap(ZL_Compressor_buildBitSplitNode(
                    compressor.get(), widths.data(), widths.size()));
            nodes.push_back(node);
        };
        // width=5
        registerNode({ 5 });
        // width=8
        registerNode(std::vector<uint8_t>(size_t(8), uint8_t(1)));
        registerNode({ 1, 3, 4 });
        // width=13
        registerNode({ 6, 7 });
        // width=16
        registerNode(std::vector<uint8_t>(size_t(16), uint8_t(1)));
        registerNode({ 1, 3, 3, 9 });
        // width=31
        registerNode({ 30, 1 });
        // width=32
        registerNode(std::vector<uint8_t>(size_t(32), uint8_t(1)));
        registerNode({ 15, 9, 3, 3, 2 });
        // width=45
        registerNode({ 32, 8, 5 });
        // width=64
        registerNode(std::vector<uint8_t>(size_t(64), uint8_t(1)));
        registerNode({ 63, 1 });
        return nodes;
    }

    std::vector<NodeID> generateNodes(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<NodeID> nodes;
        for (size_t i = 0; i < num; ++i) {
            std::vector<uint8_t> widths;
            size_t remaining = gen.usize_range("nonzero_bits", 1, 64);
            while (remaining > 0) {
                auto w = gen.usize_range("width", 1, remaining);
                widths.push_back(static_cast<uint8_t>(w));
                remaining -= w;
            }
            auto node = compressor.unwrap(ZL_Compressor_buildBitSplitNode(
                    compressor.get(), widths.data(), widths.size()));
            nodes.push_back(node);
        }
        return nodes;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        // The only possible input that is valid for all nodes is a u64 stream
        // of zeros and ones.
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(
                U64OpenZLInput::make(
                        std::vector<uint64_t>{ 0, 1, 0, 1, 1, 0, 0 }));
        inputs.push_back(
                U64OpenZLInput::make(std::vector<uint64_t>(size_t(1000), 0)));
        inputs.push_back(
                U64OpenZLInput::make(std::vector<uint64_t>(size_t(1000), 1)));

        return inputs;
    }

    size_t getTotalWidth(const Compressor& compressor, GraphID graphID) const
    {
        auto nodeID =
                ZL_Compressor_Graph_getHeadNode(compressor.get(), graphID);
        auto params =
                ZL_Compressor_Node_getLocalParams(compressor.get(), nodeID);
        if (params.copyParams.nbCopyParams != 1) {
            throw std::runtime_error("bad widths");
        }
        poly::span<const uint8_t> widths{
            (const uint8_t*)params.copyParams.copyParams[0].paramPtr,
            params.copyParams.copyParams[0].paramSize
        };
        auto sum = std::accumulate(widths.begin(), widths.end(), 0);
        return sum;
    }

    size_t minWidthForBits(size_t totalBits) const
    {
        assert(totalBits > 0);
        assert(totalBits <= 64);
        if (totalBits > 32) {
            return 8;
        }
        if (totalBits > 16) {
            return 4;
        }
        if (totalBits > 8) {
            return 2;
        }
        return 1;
    }

    uint64_t getMaskForBits(size_t totalBits) const
    {
        if (totalBits == 64) {
            return uint64_t(-1);
        } else {
            return (uint64_t(1) << totalBits) - 1;
        }
    }

    template <typename T>
    std::unique_ptr<OpenZLInput> makeInput(
            datagen::DataGen& gen,
            size_t maxInputSize,
            size_t totalBits) const
    {
        return NumericOpenZLInput<T>::make(gen.randVector<T>(
                "input",
                0,
                getMaskForBits(totalBits),
                maxInputSize / sizeof(T)));
    }

    std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSize,
            const Compressor& compressor,
            GraphID graphID) const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        // Grab the total width from the graph parameters
        const auto totalWidth = getTotalWidth(compressor, graphID);
        for (size_t i = 0; i < num; ++i) {
            // Select an input width that is compatible with the totalWidth
            auto width =
                    gen.choices("width", std::vector<size_t>{ 1, 2, 4, 8 });
            width = std::max(width, minWidthForBits(totalWidth));

            // Generate inputs that are valid for the (totalWidth, width) pair,
            // which means that if totalWidth != (8 * width), the top
            // (8 * width) - totalWidth bits are 0.
            switch (width) {
                case 1:
                    inputs.push_back(
                            makeInput<uint8_t>(gen, maxInputSize, totalWidth));
                    break;
                case 2:
                    inputs.push_back(
                            makeInput<uint16_t>(gen, maxInputSize, totalWidth));
                    break;
                case 4:
                    inputs.push_back(
                            makeInput<uint32_t>(gen, maxInputSize, totalWidth));
                    break;
                case 8:
                    inputs.push_back(
                            makeInput<uint64_t>(gen, maxInputSize, totalWidth));
                    break;
                default:
                    throw std::runtime_error("impossible");
            }
        }
        return inputs;
    }
};

class BitSplitTop8Component : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "BitSplitTop8";
    }

    int minFormatVersion() const override
    {
        return 24;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::BitsplitTop8{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        // u32 inputs
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{ 0 }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 0, 1, 2, 3, 4 }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ UINT32_MAX }));
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{ 0xABCD }));
        // Effective width = 8 boundary (max = 0xFF)
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{ 0xFF, 0x01, 0x80, 0x00 }));
        // Effective width = 9 boundary (max = 0x1FF)
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{ 0x1FF, 0x100, 0x0FF, 0x000 }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 0, 0, 0 }));
        // u8 inputs
        inputs.push_back(
                U8OpenZLInput::make(std::vector<uint8_t>{ 0, 1, 127, 255 }));
        // u16 inputs
        inputs.push_back(
                U16OpenZLInput::make(
                        std::vector<uint16_t>{ 0, 1, UINT16_MAX }));
        inputs.push_back(
                U16OpenZLInput::make(
                        std::vector<uint16_t>{ 0, 1, UINT16_MAX / 2 }));
        inputs.push_back(
                U16OpenZLInput::make(
                        std::vector<uint16_t>{ 0, 1, UINT16_MAX / 4 }));
        // u64 inputs
        inputs.push_back(
                U64OpenZLInput::make(std::vector<uint64_t>{ 0, 1, 1000 }));
        inputs.push_back(
                U64OpenZLInput::make(
                        std::vector<uint64_t>{ 0, 1, UINT64_MAX }));
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

std::unique_ptr<OpenZLComponent> makeBitSplitComponent()
{
    return std::make_unique<BitSplitComponent>();
}

std::unique_ptr<OpenZLComponent> makeBitSplitTop8Component()
{
    return std::make_unique<BitSplitTop8Component>();
}

} // namespace openzl::tests::components
