// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Conversion.hpp"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

// Helper to create a StringOpenZLInput from an initializer list of C strings.
static std::unique_ptr<OpenZLInput> makeStrings(
        std::initializer_list<const char*> strs)
{
    std::vector<std::string> v(strs.begin(), strs.end());
    return StringOpenZLInput::make(
            poly::span<const std::string>(v.data(), v.size()));
}

// ---- ConvertStructToSerial: Struct -> Serial ----

class ConvertStructToSerialComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "ConvertStructToSerial";
    }

    int minFormatVersion() const override
    {
        return 3;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::ConvertStructToSerial{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(StructOpenZLInput::make(std::string(), 2));
        inputs.push_back(
                StructOpenZLInput::make("\x01\x02\x03\x04\x05\x06\x07\x08", 2));
        inputs.push_back(
                StructOpenZLInput::make("\x01\x02\x03\x04\x05\x06\x07\x08", 4));
        inputs.push_back(StructOpenZLInput::make(std::string(8, '\xAB'), 8));
        // width=1
        inputs.push_back(StructOpenZLInput::make("\x01\x02\x03\x04", 1));
        // odd widths
        inputs.push_back(
                StructOpenZLInput::make("\x01\x02\x03\x04\x05\x06", 3));
        inputs.push_back(StructOpenZLInput::make(std::string(7, '\xCD'), 7));
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
            auto width = gen.usize_range("width", 1, 32);
            auto data  = gen.randStringWithQuantizedLength(
                    "data", maxInputSize, width);
            inputs.push_back(StructOpenZLInput::make(std::move(data), width));
        }
        return inputs;
    }
};

// ---- ConvertSerialToStruct: Serial -> Struct (parameterized by struct size)
// ----

class ConvertSerialToStructComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "ConvertSerialToStruct";
    }

    int minFormatVersion() const override
    {
        return 3;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return {
            nodes::ConvertSerialToStruct{ 1 }.parameterize(compressor),
            nodes::ConvertSerialToStruct{ 2 }.parameterize(compressor),
            nodes::ConvertSerialToStruct{ 3 }.parameterize(compressor),
            nodes::ConvertSerialToStruct{ 4 }.parameterize(compressor),
            nodes::ConvertSerialToStruct{ 8 }.parameterize(compressor),
            nodes::ConvertSerialToStruct{ 16 }.parameterize(compressor),
        };
    }

    std::vector<NodeID> generateNodes(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        // Struct sizes must divide the LCM of all predefined sizes (48)
        // so inputs aligned to 48 work with all nodes.
        constexpr size_t sizes[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 48 };
        std::vector<NodeID> result;
        result.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            auto idx = gen.usize_range("size_idx", 0, 9);
            result.push_back(
                    nodes::ConvertSerialToStruct{ static_cast<int>(sizes[idx]) }
                            .parameterize(compressor));
        }
        return result;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(SerialOpenZLInput::make(""));
        inputs.push_back(SerialOpenZLInput::make(std::string(48, '\x01')));
        inputs.push_back(SerialOpenZLInput::make(std::string(96, '\x02')));
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
            // Align to LCM of all predefined struct sizes
            // predefined: {1, 2, 3, 4, 8, 16}, LCM = 48
            auto data =
                    gen.randStringWithQuantizedLength("data", maxInputSize, 48);
            inputs.push_back(SerialOpenZLInput::make(std::move(data)));
        }
        return inputs;
    }
};

// ---- ConvertNumToSerialLE: Numeric -> Serial (LE) ----

class ConvertNumToSerialLEComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "ConvertNumToSerialLE";
    }

    int minFormatVersion() const override
    {
        return 3;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::ConvertNumToSerialLE{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 1, 2, 3 }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 0, UINT32_MAX }));
        // Single-element inputs for each width
        inputs.push_back(U8OpenZLInput::make(std::vector<uint8_t>{ 42 }));
        inputs.push_back(U16OpenZLInput::make(std::vector<uint16_t>{ 1000 }));
        inputs.push_back(U64OpenZLInput::make(std::vector<uint64_t>{ 999 }));
        // Multi-element inputs for each width
        inputs.push_back(
                U8OpenZLInput::make(std::vector<uint8_t>{ 0, 1, 127, 255 }));
        inputs.push_back(
                U16OpenZLInput::make(
                        std::vector<uint16_t>{ 0, 1, UINT16_MAX }));
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

// ---- ConvertNumToStructLE: Numeric -> Struct (LE) ----

class ConvertNumToStructLEComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "ConvertNumToStructLE";
    }

    int minFormatVersion() const override
    {
        return 3;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::ConvertNumToStructLE{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 1, 2, 3 }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 0, UINT32_MAX }));
        inputs.push_back(
                U8OpenZLInput::make(std::vector<uint8_t>{ 0, 1, 255 }));
        inputs.push_back(
                U16OpenZLInput::make(
                        std::vector<uint16_t>{ 0, 1, UINT16_MAX }));
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

// ---- ConvertStructToNumLE: Struct -> Numeric (LE) ----

class ConvertStructToNumLEComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "ConvertStructToNumLE";
    }

    int minFormatVersion() const override
    {
        return 3;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::ConvertStructToNumLE{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(StructOpenZLInput::make(std::string(), 4));
        inputs.push_back(StructOpenZLInput::make(std::string(4, '\x01'), 1));
        inputs.push_back(
                StructOpenZLInput::make("\x01\x02\x03\x04\x05\x06\x07\x08", 2));
        inputs.push_back(
                StructOpenZLInput::make("\x01\x02\x03\x04\x05\x06\x07\x08", 4));
        inputs.push_back(StructOpenZLInput::make(std::string(8, '\xAB'), 8));
        return inputs;
    }

    std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSize,
            const Compressor&,
            GraphID) const override
    {
        constexpr size_t widths[] = { 1, 2, 4, 8 };
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            auto width = widths[gen.usize_range("width_idx", 0, 3)];
            auto data  = gen.randStringWithQuantizedLength(
                    "data", maxInputSize, width);
            inputs.push_back(StructOpenZLInput::make(std::move(data), width));
        }
        return inputs;
    }
};

// ---- ConvertStructToNumBE: Struct -> Numeric (BE) ----

class ConvertStructToNumBEComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "ConvertStructToNumBE";
    }

    int minFormatVersion() const override
    {
        return 21;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::ConvertStructToNumBE{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(StructOpenZLInput::make(std::string(), 4));
        inputs.push_back(StructOpenZLInput::make(std::string(4, '\x01'), 1));
        inputs.push_back(
                StructOpenZLInput::make("\x01\x02\x03\x04\x05\x06\x07\x08", 2));
        inputs.push_back(
                StructOpenZLInput::make("\x01\x02\x03\x04\x05\x06\x07\x08", 4));
        inputs.push_back(StructOpenZLInput::make(std::string(8, '\xAB'), 8));
        return inputs;
    }

    std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSize,
            const Compressor&,
            GraphID) const override
    {
        constexpr size_t widths[] = { 1, 2, 4, 8 };
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            auto width = widths[gen.usize_range("width_idx", 0, 3)];
            auto data  = gen.randStringWithQuantizedLength(
                    "data", maxInputSize, width);
            inputs.push_back(StructOpenZLInput::make(std::move(data), width));
        }
        return inputs;
    }
};

// ---- ConvertSerialToNumLE: Serial -> Numeric (LE, all widths) ----

class ConvertSerialToNumLEComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "ConvertSerialToNumLE";
    }

    int minFormatVersion() const override
    {
        return 3;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return {
            nodes::ConvertSerialToNum8{}.parameterize(compressor),
            nodes::ConvertSerialToNumLE16{}.parameterize(compressor),
            nodes::ConvertSerialToNumLE32{}.parameterize(compressor),
            nodes::ConvertSerialToNumLE64{}.parameterize(compressor),
        };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(SerialOpenZLInput::make(""));
        inputs.push_back(SerialOpenZLInput::make(std::string(8, '\x01')));
        inputs.push_back(SerialOpenZLInput::make(std::string(16, '\xAB')));
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
            // Align to 8 so input works with all numeric widths (1, 2, 4, 8)
            auto data =
                    gen.randStringWithQuantizedLength("data", maxInputSize, 8);
            inputs.push_back(SerialOpenZLInput::make(std::move(data)));
        }
        return inputs;
    }
};

// ---- ConvertSerialToNumBE: Serial -> Numeric (BE, 16/32/64-bit) ----

class ConvertSerialToNumBEComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "ConvertSerialToNumBE";
    }

    int minFormatVersion() const override
    {
        return 21;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return {
            nodes::ConvertSerialToNumBE16{}.parameterize(compressor),
            nodes::ConvertSerialToNumBE32{}.parameterize(compressor),
            nodes::ConvertSerialToNumBE64{}.parameterize(compressor),
        };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(SerialOpenZLInput::make(""));
        inputs.push_back(SerialOpenZLInput::make(std::string(8, '\x01')));
        inputs.push_back(SerialOpenZLInput::make(std::string(16, '\xAB')));
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
            // Align to 8 so input works with all numeric widths (2, 4, 8)
            auto data =
                    gen.randStringWithQuantizedLength("data", maxInputSize, 8);
            inputs.push_back(SerialOpenZLInput::make(std::move(data)));
        }
        return inputs;
    }
};

// ---- SeparateStringComponents: String -> (Serial content, Numeric lengths)
// ----

class SeparateStringComponentsComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "SeparateStringComponents";
    }

    int minFormatVersion() const override
    {
        return 10;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::SeparateStringComponents{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(makeStrings({}));
        inputs.push_back(makeStrings({ "hello", "world" }));
        inputs.push_back(makeStrings({ "", "a", "", "bc", "" }));
        inputs.push_back(makeStrings({ "test" }));
        // String containing NUL bytes
        {
            std::string withNul = "hel";
            withNul.push_back('\0');
            withNul += "lo";
            std::vector<uint32_t> lens = { static_cast<uint32_t>(
                    withNul.size()) };
            inputs.push_back(
                    std::make_unique<StringOpenZLInput>(
                            std::move(withNul), std::move(lens)));
        }
        // Very long single string
        inputs.push_back([] {
            std::string longStr(1000, 'x');
            std::vector<uint32_t> lens = { 1000 };
            return std::make_unique<StringOpenZLInput>(
                    std::move(longStr), std::move(lens));
        }());
        // Many short strings (100 single-char strings)
        {
            std::string data;
            std::vector<uint32_t> lens;
            for (int i = 0; i < 100; ++i) {
                data.push_back(static_cast<char>('a' + (i % 26)));
                lens.push_back(1);
            }
            inputs.push_back(
                    std::make_unique<StringOpenZLInput>(
                            std::move(data), std::move(lens)));
        }
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
            auto totalLength = gen.usize_range("total_length", 0, maxInputSize);
            std::string data;
            std::vector<uint32_t> lens;
            while (data.size() < totalLength && gen.has_more_data()) {
                auto remaining = totalLength - data.size();
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

std::unique_ptr<OpenZLComponent> makeConvertStructToSerialComponent()
{
    return std::make_unique<ConvertStructToSerialComponent>();
}
std::unique_ptr<OpenZLComponent> makeConvertSerialToStructComponent()
{
    return std::make_unique<ConvertSerialToStructComponent>();
}
std::unique_ptr<OpenZLComponent> makeConvertNumToSerialLEComponent()
{
    return std::make_unique<ConvertNumToSerialLEComponent>();
}
std::unique_ptr<OpenZLComponent> makeConvertNumToStructLEComponent()
{
    return std::make_unique<ConvertNumToStructLEComponent>();
}
std::unique_ptr<OpenZLComponent> makeConvertStructToNumLEComponent()
{
    return std::make_unique<ConvertStructToNumLEComponent>();
}
std::unique_ptr<OpenZLComponent> makeConvertStructToNumBEComponent()
{
    return std::make_unique<ConvertStructToNumBEComponent>();
}
std::unique_ptr<OpenZLComponent> makeConvertSerialToNumLEComponent()
{
    return std::make_unique<ConvertSerialToNumLEComponent>();
}
std::unique_ptr<OpenZLComponent> makeConvertSerialToNumBEComponent()
{
    return std::make_unique<ConvertSerialToNumBEComponent>();
}
std::unique_ptr<OpenZLComponent> makeSeparateStringComponentsComponent()
{
    return std::make_unique<SeparateStringComponentsComponent>();
}
} // namespace openzl::tests::components
