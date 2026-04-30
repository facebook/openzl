// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Compress.hpp"
#include "tests/datagen/structures/CompressibleStringProducer.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"
#include "tests/utils.h"

namespace openzl::tests::components {
namespace {
class CompressGenericComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "CompressGeneric";
    }

    int minFormatVersion() const override
    {
        return 10;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        auto graph = graphs::Compress{}.parameterize(compressor);
        compressor.selectStartingGraph(graph);
        return { graph };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(SerialOpenZLInput::make(""));
        inputs.push_back(SerialOpenZLInput::make("x"));
        inputs.push_back(SerialOpenZLInput::make(kLoremTestInput));
        // Numeric input
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 1, 2, 3, 4, 5 }));
        // Struct input
        inputs.push_back(
                StructOpenZLInput::make("\x01\x02\x03\x04\x05\x06\x07\x08", 4));
        // String input
        {
            std::vector<std::string> strs = { "hello", "world", "foo" };
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
            size_t maxInputSize,
            const Compressor&,
            GraphID) const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            auto typeChoice = gen.usize_range("type", 0, 3);
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
                    // TODO: Generate other widths and use the
                    // CompressibleVectorProducer
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
                case 3: {
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
                    break;
                }
            }
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeCompressGenericComponent()
{
    return std::make_unique<CompressGenericComponent>();
}
} // namespace openzl::tests::components
