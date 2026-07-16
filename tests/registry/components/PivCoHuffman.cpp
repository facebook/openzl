// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/private_nodes.h"
#include "tests/datagen/structures/CompressibleStringProducer.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"
#include "tests/utils.h"

namespace openzl::tests::components {
namespace {

class PivCoHuffmanComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "PivCoHuffman";
    }

    int minFormatVersion() const override
    {
        return 27;
    }

    std::vector<NodeID> predefinedNodes(Compressor&) const override
    {
        return { ZL_NODE_PIVCO_HUFFMAN };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(SerialOpenZLInput::make(""));
        inputs.push_back(SerialOpenZLInput::make("x"));
        inputs.push_back(SerialOpenZLInput::make("xy"));
        inputs.push_back(SerialOpenZLInput::make("abc"));
        inputs.push_back(SerialOpenZLInput::make(std::string(1000, 'x')));
        inputs.push_back(SerialOpenZLInput::make(kLoremTestInput));
        inputs.push_back(SerialOpenZLInput::make(kAudioPCMS32LETestInput));

        std::string allBytes;
        allBytes.reserve(256);
        for (int b = 0; b < 256; ++b) {
            allBytes.push_back(static_cast<char>(b));
        }
        inputs.push_back(SerialOpenZLInput::make(std::move(allBytes)));

        // Larger than ZL_PIVCO_DEFAULT_BLOCK_SIZE (32 KiB), so the encoder
        // spans multiple blocks and records the optional block size in the
        // codec header.
        std::string multiBlock;
        multiBlock.reserve(40000);
        for (size_t i = 0; i < 40000; ++i) {
            multiBlock.push_back(
                    static_cast<char>('a' + (i * 7 + i / 13) % 11));
        }
        inputs.push_back(SerialOpenZLInput::make(std::move(multiBlock)));

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
            datagen::CompressibleStringProducer producer(
                    gen.getRandWrapper(),
                    gen.usize_range("input_size", 0, maxInputSize),
                    gen.u32_range("match_prob", 0, 100) / 100.0);
            inputs.push_back(SerialOpenZLInput::make(producer("input")));
        }
        return inputs;
    }

    std::vector<Benchmark> benchmarks(
            Compressor& compressor,
            datagen::DataGen& gen) const override
    {
        auto graph = buildTrivialGraph(compressor.get(), ZL_NODE_PIVCO_HUFFMAN);

        std::vector<OpenZLComponent::Benchmark> benchmarks;

        struct Params {
            size_t inputSize;
            size_t numInputs;
            size_t compressibility;
        };
        // Sizes tuned to provide approximately equal weight to each benchmark
        // in compression speed.
        constexpr std::array<Params, 9> kParams = {
            Params{ 100, 600, 10 },   Params{ 1000, 500, 10 },
            Params{ 10000, 150, 10 }, Params{ 100000, 20, 10 },
            Params{ 100000, 4, 20 },  Params{ 100000, 4, 40 },
            Params{ 100000, 4, 60 },  Params{ 100000, 4, 80 },
            Params{ 100000, 4, 90 },
        };

        benchmarks.reserve(kParams.size());
        for (const auto& params : kParams) {
            std::vector<std::unique_ptr<OpenZLInput>> inputs;
            inputs.reserve(params.numInputs);
            datagen::CompressibleStringProducer producer(
                    gen.getRandWrapper(),
                    params.inputSize,
                    params.compressibility / 100.0);
            for (size_t i = 0; i < params.numInputs; ++i) {
                inputs.push_back(SerialOpenZLInput::make(producer("input")));
            }
            OpenZLComponent::Benchmark benchmark = {
                .name = "InputSize:" + std::to_string(params.inputSize)
                        + "/Compressibility:"
                        + std::to_string(params.compressibility),
                .graph  = graph,
                .inputs = std::move(inputs),
            };
            benchmarks.push_back(std::move(benchmark));
        }

        return benchmarks;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makePivCoHuffmanComponent()
{
    return std::make_unique<PivCoHuffmanComponent>();
}

} // namespace openzl::tests::components
