// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/FieldLz.hpp"
#include "openzl/codecs/zl_store.h"
#include "tests/datagen/structures/CompressibleVectorProducer.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {
class FieldLzComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "FieldLz";
    }

    int minFormatVersion() const override
    {
        return 3;
    }

    // FieldLz node produces 5 outputs (literals, tokens, offsets,
    // extra literal lengths, extra match lengths). When used with
    // Store successors, output can be much larger than input.
    size_t compressBound(poly::span<const Input> inputs) const override
    {
        return 16 * this->OpenZLComponent::compressBound(inputs);
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        return {
            graphs::FieldLz{}(compressor),
            graphs::FieldLz{ 7 }(compressor),
        };
    }

    std::vector<GraphID> generateGraphs(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<GraphID> graphs;
        graphs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            graphs.push_back(
                    openzl::graphs::FieldLz{ gen.i32_range(
                            "compression_level", 0, 7) }(compressor));
        }
        return graphs;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return {
            nodes::FieldLz{}.parameterize(compressor),
            nodes::FieldLz{ 1 }.parameterize(compressor),
        };
    }

    std::vector<NodeID> generateNodes(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<NodeID> result;
        result.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            result.push_back(
                    nodes::FieldLz{ gen.i32_range("compression_level", 0, 7) }
                            .parameterize(compressor));
        }
        return result;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        // Empty struct input
        inputs.push_back(StructOpenZLInput::make("", 2));
        // width=2, repeated pattern (good for LZ matches)
        inputs.push_back(
                StructOpenZLInput::make(
                        "\x01\x02\x03\x04\x01\x02\x03\x04\x01\x02\x03\x04", 2));
        // width=4, single element
        inputs.push_back(StructOpenZLInput::make("\x01\x02\x03\x04", 4));
        // width=2, repeated data (lots of LZ matches)
        std::string repeated;
        for (int i = 0; i < 50; ++i) {
            repeated += "abcdefgh";
        }
        inputs.push_back(StructOpenZLInput::make(std::move(repeated), 2));
        // width=8, two elements
        inputs.push_back(StructOpenZLInput::make(std::string(16, '\xAB'), 8));
        // Numeric input
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{ 1, 2, 3, 1, 2, 3 }));
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
            constexpr size_t widths[] = { 2, 4, 8 };
            auto widthIdx             = gen.usize_range("width_idx", 0, 2);
            auto width                = widths[widthIdx];
            auto maxElts              = maxInputSize / width;
            auto numElts              = gen.usize_range("num_elts", 0, maxElts);
            auto matchProb = gen.u32_range("match_prob", 0, 100) / 100.0;
            bool useStruct = gen.boolean("use_struct");
            switch (width) {
                case 2: {
                    datagen::CompressibleVectorProducer<uint16_t> producer(
                            gen.getRandWrapper(), numElts, matchProb);
                    auto data = producer("data");
                    if (useStruct) {
                        inputs.push_back(StructOpenZLInput::make(data));
                    } else {
                        inputs.push_back(U16OpenZLInput::make(std::move(data)));
                    }
                    break;
                }
                case 4: {
                    datagen::CompressibleVectorProducer<uint32_t> producer(
                            gen.getRandWrapper(), numElts, matchProb);
                    auto data = producer("data");
                    if (useStruct) {
                        inputs.push_back(StructOpenZLInput::make(data));
                    } else {
                        inputs.push_back(U32OpenZLInput::make(std::move(data)));
                    }
                    break;
                }
                case 8: {
                    datagen::CompressibleVectorProducer<uint64_t> producer(
                            gen.getRandWrapper(), numElts, matchProb);
                    auto data = producer("data");
                    if (useStruct) {
                        inputs.push_back(StructOpenZLInput::make(data));
                    } else {
                        inputs.push_back(U64OpenZLInput::make(std::move(data)));
                    }
                    break;
                }
            }
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeFieldLzComponent()
{
    return std::make_unique<FieldLzComponent>();
}
} // namespace openzl::tests::components
