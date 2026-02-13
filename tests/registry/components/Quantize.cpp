// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Quantize.hpp"
#include "openzl/codecs/zl_store.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

// ---- QuantizeOffsets ----

class QuantizeOffsetsComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "QuantizeOffsets";
    }

    int minFormatVersion() const override
    {
        return 3;
    }

    // Quantize produces codes + extra_bits, roughly 2-3x expansion.
    size_t compressBound(poly::span<const Input> inputs) const override
    {
        size_t totalSrcSize = 0;
        for (const auto& input : inputs) {
            totalSrcSize += input.contentSize();
        }
        return ZL_compressBound(4 * totalSrcSize);
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        return { nodes::QuantizeOffsets{}(
                compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        // Empty input
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
        // All values must be non-zero for QuantizeOffsets
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{ 1, 2, 4, 8, 16, 100, 1000 }));
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{ 1 }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 1, 1, 1, 1 }));
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{ UINT32_MAX, UINT32_MAX / 2 }));
        // Powers of 2
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{
                                1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024 }));
        // Consecutive values
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 1, 2, 3, 4, 5 }));
        // Single very large value
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ UINT32_MAX }));
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
            auto maxElts = maxInputSize / sizeof(uint32_t);
            // Values must be non-zero
            auto vec =
                    gen.randVector<uint32_t>("values", 1, UINT32_MAX, maxElts);
            if (vec.empty()) {
                vec.push_back(1);
            }
            inputs.push_back(U32OpenZLInput::make(std::move(vec)));
        }
        return inputs;
    }
};

// ---- QuantizeLengths ----

class QuantizeLengthsComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "QuantizeLengths";
    }

    int minFormatVersion() const override
    {
        return 3;
    }

    // Quantize produces codes + extra_bits, roughly 2-3x expansion.
    size_t compressBound(poly::span<const Input> inputs) const override
    {
        size_t totalSrcSize = 0;
        for (const auto& input : inputs) {
            totalSrcSize += input.contentSize();
        }
        return ZL_compressBound(4 * totalSrcSize);
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        return { nodes::QuantizeLengths{}(
                compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        // Empty input
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{
                                0, 1, 2, 10, 32, 33, 100, 1000 }));
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{ 0 }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 1, 1, 1, 1 }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 0, 0, 0 }));
        // Values at the boundary=32 transition
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 31, 32, 33 }));
        // Single very large value
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ UINT32_MAX }));
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
            auto maxElts = maxInputSize / sizeof(uint32_t);
            auto vec =
                    gen.randVector<uint32_t>("values", 0, UINT32_MAX, maxElts);
            if (vec.empty()) {
                vec.push_back(0);
            }
            inputs.push_back(U32OpenZLInput::make(std::move(vec)));
        }
        return inputs;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makeQuantizeOffsetsComponent()
{
    return std::make_unique<QuantizeOffsetsComponent>();
}
std::unique_ptr<OpenZLComponent> makeQuantizeLengthsComponent()
{
    return std::make_unique<QuantizeLengthsComponent>();
}
} // namespace openzl::tests::components
