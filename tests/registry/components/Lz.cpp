// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/zl_lz.h"
#include "tests/datagen/structures/CompressibleStringProducer.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"
#include "tests/utils.h"

namespace openzl::tests::components {
namespace {
class LzComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Lz";
    }

    int minFormatVersion() const override
    {
        return 24;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        std::vector<NodeID> nodes;
        nodes.push_back(ZL_NODE_LZ);
        LocalParams params;
        params.addIntParam(ZL_LzParam_compressionLevel, -2);
        nodes.push_back(compressor.parameterizeNode(
                ZL_NODE_LZ,
                NodeParameters{ .localParams = std::move(params) }));
        return nodes;
    }

    static GraphID lzWithLevel(Compressor& compressor, int level)
    {
        LocalParams params;
        params.addIntParam(ZL_LzParam_compressionLevel, level);
        return compressor.parameterizeGraph(
                ZL_GRAPH_LZ,
                GraphParameters{ .localParams = std::move(params) });
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        std::vector<GraphID> graphs;
        graphs.push_back(ZL_GRAPH_LZ);
        graphs.push_back(lzWithLevel(compressor, -2));
        return graphs;
    }

    static void maybeSetParam(
            LocalParams& params,
            datagen::DataGen& gen,
            int param,
            int min,
            int max)
    {
        if (gen.u8_range("set_param", 0, 1) == 1) {
            params.addIntParam(param, gen.i32_range("param", min, max));
        }
    }

    std::vector<GraphID> generateGraphs(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<GraphID> graphs;
        graphs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            LocalParams params;
            maybeSetParam(params, gen, ZL_LzParam_compressionLevel, -10, 10);
            maybeSetParam(params, gen, ZL_LzParam_acceleration, -10, 100);
            graphs.push_back(compressor.parameterizeGraph(
                    ZL_GRAPH_LZ,
                    GraphParameters{ .localParams = std::move(params) }));
        }
        return graphs;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        // Empty input
        inputs.push_back(std::make_unique<SerialOpenZLInput>(""));
        // Single byte
        inputs.push_back(std::make_unique<SerialOpenZLInput>("x"));
        // Short input (shorter than minimum match length)
        inputs.push_back(std::make_unique<SerialOpenZLInput>("abc"));
        // Repeated bytes (should produce matches)
        inputs.push_back(
                std::make_unique<SerialOpenZLInput>(std::string(1000, 'x')));
        inputs.push_back(
                std::make_unique<SerialOpenZLInput>(std::string(100000, 'x')));
        // Input with repeating pattern (good for LZ matching)
        {
            std::string pattern;
            for (int i = 0; i < 100; ++i) {
                pattern += "Hello World! ";
            }
            inputs.push_back(std::make_unique<SerialOpenZLInput>(pattern));
        }
        // Standard test data
        inputs.push_back(std::make_unique<SerialOpenZLInput>(kLoremTestInput));
        inputs.push_back(
                std::make_unique<SerialOpenZLInput>(kAudioPCMS32LETestInput));
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
            inputs.push_back(
                    std::make_unique<SerialOpenZLInput>(producer("input")));
        }
        return inputs;
    }

    std::vector<Benchmark> benchmarks(
            Compressor& compressor,
            datagen::DataGen& gen) const override
    {
        struct Param {
            size_t inputSize;
            size_t numInputs;
            int compressionLevel;
        };
        constexpr std::array<Param, 6> kParams = {
            Param{ 1000, 200, 1 },  Param{ 10000, 50, 1 },
            Param{ 100000, 10, 1 }, Param{ 1000, 200, -1 },
            Param{ 10000, 50, -1 }, Param{ 100000, 10, -1 },
        };

        std::vector<Benchmark> benchmarks;
        benchmarks.reserve(kParams.size());
        for (const auto& param : kParams) {
            datagen::CompressibleStringProducer producer(
                    gen.getRandWrapper(), param.inputSize);
            std::vector<std::unique_ptr<OpenZLInput>> inputs;
            inputs.reserve(param.numInputs);
            for (size_t i = 0; i < param.numInputs; ++i) {
                inputs.push_back(
                        std::make_unique<SerialOpenZLInput>(producer("input")));
            }
            benchmarks.push_back(
                    Benchmark{
                            .name = "InputSize:"
                                    + std::to_string(param.inputSize)
                                    + "/CompressionLevel:"
                                    + std::to_string(param.compressionLevel),
                            .graph = lzWithLevel(
                                    compressor, param.compressionLevel),
                            .inputs = std::move(inputs),
                    });
        }
        return benchmarks;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeLzComponent()
{
    return std::make_unique<LzComponent>();
}
} // namespace openzl::tests::components
