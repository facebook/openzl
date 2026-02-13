// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Tokenize.hpp"
#include "openzl/codecs/zl_store.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

// ---- TokenizeStruct ----

class TokenizeStructComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "TokenizeStruct";
    }

    int minFormatVersion() const override
    {
        return 8;
    }

    // Tokenize produces alphabet + indices, which can be larger than input.
    // For width=1: N bytes input -> alphabet (up to 256) + indices (4N).
    size_t compressBound(poly::span<const Input> inputs) const override
    {
        return 6 * this->OpenZLComponent::compressBound(inputs);
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        return { nodes::TokenizeStruct{}(
                compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        // Empty struct input
        inputs.push_back(StructOpenZLInput::make("", 2));
        // width=2, repeated elements
        inputs.push_back(
                StructOpenZLInput::make("\x01\x02\x03\x04\x01\x02\x03\x04", 2));
        // width=1, repeated bytes
        inputs.push_back(StructOpenZLInput::make("aaabbbccc", 1));
        // width=4, single element
        inputs.push_back(StructOpenZLInput::make("\x01\x02\x03\x04", 4));
        // width=2, all unique
        inputs.push_back(
                StructOpenZLInput::make("\x01\x02\x03\x04\x05\x06", 2));
        // width=8
        inputs.push_back(StructOpenZLInput::make(std::string(16, '\xAB'), 8));
        // Large alphabet (many unique width=1 elements)
        {
            std::string data;
            for (int i = 0; i < 64; ++i) {
                data.push_back(static_cast<char>(i));
            }
            inputs.push_back(StructOpenZLInput::make(std::move(data), 1));
        }
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
            // Encoder only supports widths 1, 2, 4, 8
            constexpr size_t widths[] = { 1, 2, 4, 8 };
            auto widthIdx             = gen.usize_range("width_idx", 0, 3);
            auto width                = widths[widthIdx];
            auto data                 = gen.randStringWithQuantizedLength(
                    "data", maxInputSize, width);
            inputs.push_back(StructOpenZLInput::make(std::move(data), width));
        }
        return inputs;
    }
};

// ---- TokenizeNumeric ----

class TokenizeNumericComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "TokenizeNumeric";
    }

    int minFormatVersion() const override
    {
        return 8;
    }

    // Tokenize produces alphabet + indices, up to 2x expansion for u32.
    size_t compressBound(poly::span<const Input> inputs) const override
    {
        return 4 * this->OpenZLComponent::compressBound(inputs);
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        return {
            nodes::TokenizeNumeric{ false }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
            nodes::TokenizeNumeric{ true }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
        };
    }

    std::vector<GraphID> generateGraphs(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        // Already covered by predefined graphs.
        return {};
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        // Empty input
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{ 1, 2, 3, 1, 2, 3 }));
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{ 42 }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 5, 5, 5, 5 }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 0, 1, 2, 3, 4 }));
        // u8 input
        inputs.push_back(
                U8OpenZLInput::make(std::vector<uint8_t>{ 1, 2, 3, 1, 2, 3 }));
        // u16 input
        inputs.push_back(
                U16OpenZLInput::make(std::vector<uint16_t>{ 10, 20, 10, 20 }));
        // u64 input
        inputs.push_back(
                U64OpenZLInput::make(std::vector<uint64_t>{ 100, 200, 100 }));
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

// ---- TokenizeString ----

class TokenizeStringComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "TokenizeString";
    }

    int minFormatVersion() const override
    {
        return 11;
    }

    // Tokenize produces alphabet + indices.
    size_t compressBound(poly::span<const Input> inputs) const override
    {
        return 6 * this->OpenZLComponent::compressBound(inputs);
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        return {
            nodes::TokenizeString{ false }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
            nodes::TokenizeString{ true }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
        };
    }

    std::vector<GraphID> generateGraphs(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        // Already covered by predefined graphs.
        return {};
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::string> s1 = { "hello", "world", "hello", "world" };
        std::vector<std::string> s2 = { "a" };
        std::vector<std::string> s3 = { "foo", "bar", "baz", "foo" };
        // Empty string list
        std::vector<std::string> s4 = {};
        // All identical strings (alphabet of 1)
        std::vector<std::string> s5 = { "same", "same", "same" };
        // Strings containing NUL bytes
        std::vector<std::string> s6 = { std::string("a\x00b", 3),
                                        std::string("a\x00b", 3),
                                        std::string("c\x00d", 3) };

        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(
                StringOpenZLInput::make(
                        poly::span<const std::string>(s1.data(), s1.size())));
        inputs.push_back(
                StringOpenZLInput::make(
                        poly::span<const std::string>(s2.data(), s2.size())));
        inputs.push_back(
                StringOpenZLInput::make(
                        poly::span<const std::string>(s3.data(), s3.size())));
        inputs.push_back(
                StringOpenZLInput::make(
                        poly::span<const std::string>(s4.data(), s4.size())));
        inputs.push_back(
                StringOpenZLInput::make(
                        poly::span<const std::string>(s5.data(), s5.size())));
        inputs.push_back(
                StringOpenZLInput::make(
                        poly::span<const std::string>(s6.data(), s6.size())));
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
            size_t totalLength =
                    gen.usize_range("total_length", 0, maxInputSize);
            std::string data;
            std::vector<uint32_t> lens;
            while (data.size() < totalLength && gen.has_more_data()) {
                auto remaining = maxInputSize - data.size();
                auto str       = gen.randString("str", remaining);
                data += str;
                lens.push_back(str.size());
            }
            inputs.push_back(
                    std::make_unique<StringOpenZLInput>(
                            std::move(data), std::move(lens)));
        }
        return inputs;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makeTokenizeStructComponent()
{
    return std::make_unique<TokenizeStructComponent>();
}
std::unique_ptr<OpenZLComponent> makeTokenizeNumericComponent()
{
    return std::make_unique<TokenizeNumericComponent>();
}
std::unique_ptr<OpenZLComponent> makeTokenizeStringComponent()
{
    return std::make_unique<TokenizeStringComponent>();
}
} // namespace openzl::tests::components
