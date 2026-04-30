// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Concat.hpp"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

// ---- ConcatSerial ----

class ConcatSerialComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "ConcatSerial";
    }

    int minFormatVersion() const override
    {
        return 16;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::ConcatSerial{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        // Single input (trivial concat)
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(SerialOpenZLInput::make("hello"));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(SerialOpenZLInput::make("hello"));
            inner.push_back(SerialOpenZLInput::make("world"));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(SerialOpenZLInput::make(""));
            inner.push_back(SerialOpenZLInput::make("x"));
            inner.push_back(SerialOpenZLInput::make(""));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // Many inputs (5)
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(SerialOpenZLInput::make("a"));
            inner.push_back(SerialOpenZLInput::make("bb"));
            inner.push_back(SerialOpenZLInput::make("ccc"));
            inner.push_back(SerialOpenZLInput::make("dd"));
            inner.push_back(SerialOpenZLInput::make("e"));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
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
            auto totalSize = gen.usize_range("total_size", 0, maxInputSize);
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            for (size_t size = 0; gen.has_more_data() && inner.size() < 50
                 && size < totalSize;) {
                auto nextSize = gen.usize_range("size", 0, totalSize - size);
                auto str      = gen.randString("data", nextSize);
                size += str.size();
                inner.push_back(SerialOpenZLInput::make(std::move(str)));
            }
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        return inputs;
    }
};

// ---- ConcatNumeric ----

class ConcatNumericComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "ConcatNumeric";
    }

    int minFormatVersion() const override
    {
        return 17;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::ConcatNumeric{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(
                    U32OpenZLInput::make(std::vector<uint32_t>{ 1, 2 }));
            inner.push_back(
                    U32OpenZLInput::make(std::vector<uint32_t>{ 3, 4 }));
            inner.push_back(
                    U32OpenZLInput::make(std::vector<uint32_t>{ 5, 6 }));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
            inner.push_back(U32OpenZLInput::make(std::vector<uint32_t>{ 42 }));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // u8 inputs
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(
                    U8OpenZLInput::make(std::vector<uint8_t>{ 1, 2, 3 }));
            inner.push_back(U8OpenZLInput::make(std::vector<uint8_t>{ 4, 5 }));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // u16 inputs
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(
                    U16OpenZLInput::make(std::vector<uint16_t>{ 100, 200 }));
            inner.push_back(U16OpenZLInput::make(std::vector<uint16_t>{ 300 }));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // u64 inputs
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(
                    U64OpenZLInput::make(std::vector<uint64_t>{ 1, 2 }));
            inner.push_back(U64OpenZLInput::make(std::vector<uint64_t>{ 3 }));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // Multiple empty inputs
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
            inner.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
            inner.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
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
            auto totalSize = gen.usize_range(
                    "total_size", 0, maxInputSize / sizeof(uint32_t));
            auto widthChoice = gen.usize_range("width", 0, 3);
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            for (size_t size = 0; gen.has_more_data() && inner.size() < 50
                 && size < totalSize;) {
                auto maxElts = totalSize - size;
                switch (widthChoice) {
                    case 0: {
                        auto data = gen.randVector<uint8_t>(
                                "values", 0, UINT8_MAX, maxElts);
                        size += data.size();
                        inner.push_back(U8OpenZLInput::make(std::move(data)));
                        break;
                    }
                    case 1: {
                        auto data = gen.randVector<uint16_t>(
                                "values", 0, UINT16_MAX, maxElts);
                        size += data.size();
                        inner.push_back(U16OpenZLInput::make(std::move(data)));
                        break;
                    }
                    case 2: {
                        auto data = gen.randVector<uint32_t>(
                                "values", 0, UINT32_MAX, maxElts);
                        size += data.size();
                        inner.push_back(U32OpenZLInput::make(std::move(data)));
                        break;
                    }
                    case 3: {
                        auto data = gen.randVector<uint64_t>(
                                "values", 0, UINT64_MAX, maxElts);
                        size += data.size();
                        inner.push_back(U64OpenZLInput::make(std::move(data)));
                        break;
                    }
                }
            }
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        return inputs;
    }
};

// ---- ConcatStruct ----

class ConcatStructComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "ConcatStruct";
    }

    int minFormatVersion() const override
    {
        return 17;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::ConcatStruct{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(StructOpenZLInput::make("\x01\x02\x03\x04", 2));
            inner.push_back(StructOpenZLInput::make("\x05\x06\x07\x08", 2));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(StructOpenZLInput::make(std::string(), 4));
            inner.push_back(StructOpenZLInput::make(std::string(8, '\x01'), 4));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // width=1 inputs
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(StructOpenZLInput::make("abc", 1));
            inner.push_back(StructOpenZLInput::make("de", 1));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // Different element counts
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(StructOpenZLInput::make("\x01\x02", 2));
            inner.push_back(
                    StructOpenZLInput::make("\x03\x04\x05\x06\x07\x08", 2));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
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
            auto width = gen.usize_range("width", 1, 32);
            auto totalSize =
                    (gen.usize_range("total_size", 0, maxInputSize) / width)
                    * width;
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            for (size_t size = 0; gen.has_more_data() && inner.size() < 50
                 && size < totalSize;) {
                auto data = gen.randStringWithQuantizedLength(
                        "data", totalSize - size, width);
                size += data.size();
                inner.push_back(
                        StructOpenZLInput::make(std::move(data), width));
            }
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        return inputs;
    }
};

// ---- ConcatString ----

class ConcatStringComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "ConcatString";
    }

    int minFormatVersion() const override
    {
        return 18;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::ConcatString{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        {
            std::vector<std::string> s1 = { "hello", "world" };
            std::vector<std::string> s2 = { "foo", "bar" };
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(
                    StringOpenZLInput::make(
                            poly::span<const std::string>(
                                    s1.data(), s1.size())));
            inner.push_back(
                    StringOpenZLInput::make(
                            poly::span<const std::string>(
                                    s2.data(), s2.size())));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        {
            std::vector<std::string> s1 = { "" };
            std::vector<std::string> s2 = { "a" };
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(
                    StringOpenZLInput::make(
                            poly::span<const std::string>(
                                    s1.data(), s1.size())));
            inner.push_back(
                    StringOpenZLInput::make(
                            poly::span<const std::string>(
                                    s2.data(), s2.size())));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // 3 inputs
        {
            std::vector<std::string> s1 = { "a", "b" };
            std::vector<std::string> s2 = { "c" };
            std::vector<std::string> s3 = { "d", "e", "f" };
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(
                    StringOpenZLInput::make(
                            poly::span<const std::string>(
                                    s1.data(), s1.size())));
            inner.push_back(
                    StringOpenZLInput::make(
                            poly::span<const std::string>(
                                    s2.data(), s2.size())));
            inner.push_back(
                    StringOpenZLInput::make(
                            poly::span<const std::string>(
                                    s3.data(), s3.size())));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // Inputs with empty string lists
        {
            std::vector<std::string> s1 = {};
            std::vector<std::string> s2 = { "x" };
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(
                    StringOpenZLInput::make(
                            poly::span<const std::string>(
                                    s1.data(), s1.size())));
            inner.push_back(
                    StringOpenZLInput::make(
                            poly::span<const std::string>(
                                    s2.data(), s2.size())));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
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
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            size_t totalSize = gen.usize_range("total_size", 0, maxInputSize);
            ;
            for (size_t size = 0; gen.has_more_data() && inner.size() < 50
                 && size < totalSize;) {
                auto streamSize =
                        gen.usize_range("stream_size", 0, totalSize - size);
                std::string data;
                std::vector<uint32_t> lens;
                while (gen.has_more_data() && data.size() < streamSize) {
                    auto remaining = streamSize - data.size();
                    auto str       = gen.randString("str", remaining);
                    data += str;
                    lens.push_back(str.size());
                }
                size += data.size();
                inner.push_back(
                        std::make_unique<StringOpenZLInput>(
                                std::move(data), std::move(lens)));
            }
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        return inputs;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makeConcatSerialComponent()
{
    return std::make_unique<ConcatSerialComponent>();
}
std::unique_ptr<OpenZLComponent> makeConcatNumericComponent()
{
    return std::make_unique<ConcatNumericComponent>();
}
std::unique_ptr<OpenZLComponent> makeConcatStructComponent()
{
    return std::make_unique<ConcatStructComponent>();
}
std::unique_ptr<OpenZLComponent> makeConcatStringComponent()
{
    return std::make_unique<ConcatStringComponent>();
}
} // namespace openzl::tests::components
