// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Entropy.hpp"
#include "tests/datagen/structures/CompressibleStringProducer.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"
#include "tests/utils.h"

namespace openzl::tests::components {
namespace {

// ---- Fse ----

class FseComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Fse";
    }

    int minFormatVersion() const override
    {
        return ZL_MIN_FORMAT_VERSION;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        auto graph = graphs::Fse{}.parameterize(compressor);
        compressor.selectStartingGraph(graph);
        return { graph };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(SerialOpenZLInput::make(""));
        inputs.push_back(SerialOpenZLInput::make("a"));
        inputs.push_back(SerialOpenZLInput::make(std::string(100, 'x')));
        inputs.push_back(SerialOpenZLInput::make(kLoremTestInput));
        // All 256 byte values
        {
            std::string allBytes;
            for (int b = 0; b < 256; ++b) {
                allBytes.push_back(static_cast<char>(b));
            }
            inputs.push_back(SerialOpenZLInput::make(std::move(allBytes)));
        }
        // Two-byte input
        inputs.push_back(SerialOpenZLInput::make("ab"));
        // Heavily skewed distribution
        {
            std::string skewed(200, 'a');
            skewed += "bcde";
            inputs.push_back(SerialOpenZLInput::make(std::move(skewed)));
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
            datagen::CompressibleStringProducer producer(
                    gen.getRandWrapper(),
                    gen.usize_range("input_size", 0, maxInputSize),
                    gen.u32_range("match_prob", 0, 100) / 100.0);
            inputs.push_back(SerialOpenZLInput::make(producer("input")));
        }
        return inputs;
    }
};

// ---- Huffman ----

class HuffmanComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Huffman";
    }

    int minFormatVersion() const override
    {
        return ZL_MIN_FORMAT_VERSION;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        auto graph = graphs::Huffman{}.parameterize(compressor);
        compressor.selectStartingGraph(graph);
        return { graph };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(SerialOpenZLInput::make(""));
        inputs.push_back(SerialOpenZLInput::make("a"));
        inputs.push_back(SerialOpenZLInput::make(std::string(100, 'x')));
        inputs.push_back(SerialOpenZLInput::make(kLoremTestInput));
        // All 256 byte values
        {
            std::string allBytes;
            for (int b = 0; b < 256; ++b) {
                allBytes.push_back(static_cast<char>(b));
            }
            inputs.push_back(SerialOpenZLInput::make(std::move(allBytes)));
        }
        // Struct input (Huffman supports struct with eltWidth 1 or 2)
        inputs.push_back(
                StructOpenZLInput::make("\x01\x02\x03\x04\x01\x02\x03\x04", 2));
        // Numeric input (u16 = eltWidth 2)
        inputs.push_back(
                U16OpenZLInput::make(
                        std::vector<uint16_t>{ 1, 2, 3, 1, 2, 3 }));
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
                    // Huffman supports struct with eltWidth 1 or 2
                    auto widthChoice = gen.usize_range("width", 0, 1);
                    auto width       = widthChoice == 0 ? 1 : 2;
                    inputs.push_back(
                            StructOpenZLInput::make(
                                    gen.randStringWithQuantizedLength(
                                            "data", maxInputSize, width),
                                    width));
                    break;
                }
                case 2: {
                    // Huffman supports u8 (eltWidth 1) and u16 (eltWidth 2)
                    if (gen.boolean("use_u8")) {
                        auto maxElts = std::min(
                                maxInputSize / sizeof(uint8_t), size_t(131072));
                        inputs.push_back(
                                U8OpenZLInput::make(gen.randVector<uint8_t>(
                                        "values", 0, UINT8_MAX, maxElts)));
                    } else {
                        auto maxElts = maxInputSize / sizeof(uint16_t);
                        inputs.push_back(
                                U16OpenZLInput::make(gen.randVector<uint16_t>(
                                        "values", 0, UINT16_MAX, maxElts)));
                    }
                    break;
                }
            }
        }
        return inputs;
    }
};

// ---- Entropy ----

class EntropyComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Entropy";
    }

    int minFormatVersion() const override
    {
        return ZL_MIN_FORMAT_VERSION;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        auto graph = graphs::Entropy{}.parameterize(compressor);
        compressor.selectStartingGraph(graph);
        return { graph };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(SerialOpenZLInput::make(""));
        inputs.push_back(SerialOpenZLInput::make("a"));
        inputs.push_back(SerialOpenZLInput::make(std::string(100, 'x')));
        inputs.push_back(SerialOpenZLInput::make(kLoremTestInput));
        // Numeric input (Entropy supports eltWidth 1 or 2)
        inputs.push_back(
                U16OpenZLInput::make(std::vector<uint16_t>{ 1, 2, 3, 4, 5 }));
        // Struct input (Entropy supports struct with eltWidth 1 or 2)
        inputs.push_back(
                StructOpenZLInput::make("\x01\x02\x03\x04\x05\x06", 2));
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
                    // Entropy supports eltWidth 1 or 2
                    if (gen.boolean("use_u8")) {
                        auto maxElts = std::min(
                                maxInputSize / sizeof(uint8_t), size_t(131072));
                        inputs.push_back(
                                U8OpenZLInput::make(gen.randVector<uint8_t>(
                                        "values", 0, UINT8_MAX, maxElts)));
                    } else {
                        auto maxElts = maxInputSize / sizeof(uint16_t);
                        inputs.push_back(
                                U16OpenZLInput::make(gen.randVector<uint16_t>(
                                        "values", 0, UINT16_MAX, maxElts)));
                    }
                    break;
                }
                case 2: {
                    // Entropy supports struct with eltWidth 1 or 2
                    auto widthChoice = gen.usize_range("width", 0, 1);
                    auto width       = widthChoice == 0 ? 1 : 2;
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

std::unique_ptr<OpenZLComponent> makeFseComponent()
{
    return std::make_unique<FseComponent>();
}
std::unique_ptr<OpenZLComponent> makeHuffmanComponent()
{
    return std::make_unique<HuffmanComponent>();
}
std::unique_ptr<OpenZLComponent> makeEntropyComponent()
{
    return std::make_unique<EntropyComponent>();
}
} // namespace openzl::tests::components
