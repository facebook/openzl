// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Store.hpp"
#include "tests/datagen/structures/CompressibleStringProducer.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {
class StoreComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Store";
    }

    int minFormatVersion() const override
    {
        return ZL_MIN_FORMAT_VERSION;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        auto graph = graphs::Store{}.parameterize(compressor);
        compressor.selectStartingGraph(graph);
        return { graph };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(SerialOpenZLInput::make(""));
        inputs.push_back(SerialOpenZLInput::make("hello"));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 1, 2, 3 }));
        inputs.push_back(SerialOpenZLInput::make(std::string(100, 'x')));
        // Struct input
        inputs.push_back(
                StructOpenZLInput::make("\x01\x02\x03\x04\x05\x06\x07\x08", 4));
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
            auto typeChoice = gen.usize_range("type", 0, 2);
            switch (typeChoice) {
                case 0: {
                    datagen::CompressibleStringProducer producer(
                            gen.getRandWrapper(),
                            gen.usize_range("input_size", 0, maxInputSize),
                            gen.u32_range("match_prob", 0, 100) / 100.0);
                    inputs.push_back(
                            SerialOpenZLInput::make(producer("input")));
                    break;
                }
                case 1: {
                    auto maxElts = maxInputSize / sizeof(uint32_t);
                    inputs.push_back(
                            U32OpenZLInput::make(gen.randVector<uint32_t>(
                                    "values", 0, UINT32_MAX, maxElts)));
                    break;
                }
                case 2: {
                    auto width = gen.usize_range("width", 1, 8);
                    inputs.push_back(
                            StructOpenZLInput::make(
                                    gen.randStringWithQuantizedLength(
                                            "data", maxInputSize, width),
                                    width));
                    break;
                }
            }
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeStoreComponent()
{
    return std::make_unique<StoreComponent>();
}

class StoreStringComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "StoreString";
    }

    int minFormatVersion() const override
    {
        return 10;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        auto graph = graphs::Store{}.parameterize(compressor);
        compressor.selectStartingGraph(graph);
        return { graph };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        // Empty string list
        {
            std::vector<std::string> strs = {};
            inputs.push_back(
                    StringOpenZLInput::make(
                            poly::span<const std::string>(
                                    strs.data(), strs.size())));
        }
        // Single string
        {
            std::vector<std::string> strs = { "hello" };
            inputs.push_back(
                    StringOpenZLInput::make(
                            poly::span<const std::string>(
                                    strs.data(), strs.size())));
        }
        // Multiple strings
        {
            std::vector<std::string> strs = { "hello", "world", "foo" };
            inputs.push_back(
                    StringOpenZLInput::make(
                            poly::span<const std::string>(
                                    strs.data(), strs.size())));
        }
        // Empty strings
        {
            std::vector<std::string> strs = { "", "", "" };
            inputs.push_back(
                    StringOpenZLInput::make(
                            poly::span<const std::string>(
                                    strs.data(), strs.size())));
        }
        // Mixed empty and non-empty
        {
            std::vector<std::string> strs = { "", "a", "", "bc", "" };
            inputs.push_back(
                    StringOpenZLInput::make(
                            poly::span<const std::string>(
                                    strs.data(), strs.size())));
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
            auto size = gen.usize_range("size", 0, maxInputSize);
            std::string data;
            std::vector<uint32_t> lens;
            for (; gen.has_more_data() && data.size() < size;) {
                auto remaining = size - data.size();
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

std::unique_ptr<OpenZLComponent> makeStoreStringComponent()
{
    return std::make_unique<StoreStringComponent>();
}
} // namespace openzl::tests::components
