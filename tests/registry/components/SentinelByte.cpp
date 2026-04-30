// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Sentinel.hpp"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

class SentinelByteComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "SentinelByte";
    }

    int minFormatVersion() const override
    {
        return 24;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        return {
            nodes::SentinelByte{}(compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
        };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        // Empty inputs (sentinel_byte rejects 1-byte inputs, so no U8)
        inputs.push_back(U16OpenZLInput::make(std::vector<uint16_t>{}));
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
        inputs.push_back(U64OpenZLInput::make(std::vector<uint64_t>{}));

        // All values < 255 (no exceptions)
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 0, 1, 42, 254 }));
        // All values >= 255 (all exceptions)
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{ 255, 256, 1000, UINT32_MAX }));
        // Mix of small and large values
        inputs.push_back(
                U32OpenZLInput::make(
                        std::vector<uint32_t>{ 0, 255, 1, 256, 254, 1000 }));
        // Single element < 255
        inputs.push_back(U16OpenZLInput::make(std::vector<uint16_t>{ 42 }));
        // Single element == 255
        inputs.push_back(U16OpenZLInput::make(std::vector<uint16_t>{ 255 }));
        // Width 8 with large values
        inputs.push_back(
                U64OpenZLInput::make(
                        std::vector<uint64_t>{ 0, 254, 255, 256, UINT64_MAX }));

        return inputs;
    }

    template <typename T>
    std::unique_ptr<OpenZLInput>
    makeInput(datagen::DataGen& gen, size_t inputSize, float smallProb) const
    {
        std::vector<T> input;
        input.reserve(inputSize);
        for (size_t i = 0; i < inputSize; ++i) {
            if (gen.coin("small", smallProb)) {
                input.push_back(
                        gen.getRandWrapper()->range("small_val", T(0), T(254)));
            } else {
                input.push_back(gen.getRandWrapper()->range<T>(
                        "large_val", T(255), std::numeric_limits<T>::max()));
            }
        }
        return NumericOpenZLInput<T>::make(std::move(input));
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
            // Only widths >= 2 (sentinel_byte rejects 1-byte inputs)
            const auto widthChoice = gen.usize_range("width", 0, 2);
            const auto smallProb   = gen.f32_range("small_prob", 0.0, 1.0);
            auto inputSize = gen.usize_range("input_size", 0, maxInputSize);
            switch (widthChoice) {
                case 0:
                    inputs.push_back(
                            makeInput<uint16_t>(gen, inputSize, smallProb));
                    break;
                case 1:
                    inputs.push_back(
                            makeInput<uint32_t>(gen, inputSize, smallProb));
                    break;
                case 2:
                    inputs.push_back(
                            makeInput<uint64_t>(gen, inputSize, smallProb));
            }
        }
        return inputs;
    }

    std::vector<Benchmark> benchmarks(
            Compressor& compressor,
            datagen::DataGen& gen) const override
    {
        constexpr size_t kInputSize = 10000;
        constexpr size_t kNumInputs = 10;

        std::array<int, 5> smallProbs = { 5, 25, 50, 75, 95 };

        std::vector<Benchmark> benchmarks;
        benchmarks.reserve(smallProbs.size());

        auto graph = nodes::SentinelByte{}(
                compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE);

        for (auto smallProb : smallProbs) {
            std::vector<std::unique_ptr<OpenZLInput>> inputs;
            inputs.reserve(kNumInputs);
            for (size_t i = 0; i < kNumInputs; ++i) {
                inputs.push_back(
                        makeInput<uint16_t>(
                                gen, kInputSize, smallProb / 100.0));
            }
            benchmarks.push_back(
                    Benchmark{
                            .name   = "SmallProb:" + std::to_string(smallProb),
                            .graph  = graph,
                            .inputs = std::move(inputs),
                    });
        }
        return benchmarks;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makeSentinelByteComponent()
{
    return std::make_unique<SentinelByteComponent>();
}

} // namespace openzl::tests::components
