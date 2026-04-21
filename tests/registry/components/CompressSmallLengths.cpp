// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/private_nodes.h"
#include "tests/datagen/distributions/ConstantDistribution.h"
#include "tests/datagen/distributions/LogUniformDistribution.h"
#include "tests/datagen/structures/VectorProducer.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

class CompressSmallLengthsComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "CompressSmallLengths";
    }

    int minFormatVersion() const override
    {
        return ZL_MIN_FORMAT_VERSION;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        compressor.selectStartingGraph(ZL_GRAPH_COMPRESS_SMALL_LENGTHS);
        return { ZL_GRAPH_COMPRESS_SMALL_LENGTHS };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;

        // Empty inputs at each width
        inputs.push_back(U8OpenZLInput::make(std::vector<uint8_t>{}));
        inputs.push_back(U16OpenZLInput::make(std::vector<uint16_t>{}));
        inputs.push_back(U32OpenZLInput::make(std::vector<uint32_t>{}));
        inputs.push_back(U64OpenZLInput::make(std::vector<uint64_t>{}));

        // Small input (< 50 elements, triggers store fallback) at each width
        inputs.push_back(
                U8OpenZLInput::make(std::vector<uint8_t>{ 0, 1, 254, 255 }));
        inputs.push_back(
                U16OpenZLInput::make(std::vector<uint16_t>{ 0, 1, 254, 500 }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 0, 1, 254, 500 }));
        inputs.push_back(
                U64OpenZLInput::make(std::vector<uint64_t>{ 0, 1, 254, 500 }));

        // All values < 255 (no exceptions in sentinel_byte)
        {
            std::vector<uint8_t> v(254);
            std::iota(v.begin(), v.end(), 0);
            inputs.push_back(U8OpenZLInput::make(std::move(v)));
        }
        {
            std::vector<uint16_t> v(254);
            std::iota(v.begin(), v.end(), 0);
            inputs.push_back(U16OpenZLInput::make(std::move(v)));
        }
        {
            std::vector<uint32_t> v(254);
            std::iota(v.begin(), v.end(), 0);
            inputs.push_back(U32OpenZLInput::make(std::move(v)));
        }
        {
            std::vector<uint64_t> v(254);
            std::iota(v.begin(), v.end(), 0);
            inputs.push_back(U64OpenZLInput::make(std::move(v)));
        }

        // All values >= 255 (all exceptions in sentinel_byte)
        {
            std::vector<uint8_t> v(1000, 255);
            inputs.push_back(U8OpenZLInput::make(std::move(v)));
        }
        {
            std::vector<uint16_t> v(1000);
            std::iota(v.begin(), v.end(), 255);
            inputs.push_back(U16OpenZLInput::make(std::move(v)));
        }
        {
            std::vector<uint32_t> v(1000);
            std::iota(v.begin(), v.end(), 255);
            inputs.push_back(U32OpenZLInput::make(std::move(v)));
        }
        {
            std::vector<uint64_t> v(1000);
            std::iota(v.begin(), v.end(), 255);
            inputs.push_back(U64OpenZLInput::make(std::move(v)));
        }

        // Mix of small and large values
        {
            std::vector<uint8_t> v(1000, 255);
            for (size_t i = 0; i < v.size(); i += 2) {
                v[i] = (uint8_t)(i % 255);
            }
            inputs.push_back(U8OpenZLInput::make(std::move(v)));
        }
        {
            std::vector<uint16_t> v(1000);
            for (size_t i = 0; i < v.size(); i += 2) {
                v[i]     = (uint16_t)(i % 255);
                v[i + 1] = 255 + i;
            }
            inputs.push_back(U16OpenZLInput::make(std::move(v)));
        }
        {
            std::vector<uint32_t> v(1000);
            for (size_t i = 0; i < v.size(); i += 2) {
                v[i]     = (uint32_t)(i % 255);
                v[i + 1] = 255 + i;
            }
            inputs.push_back(U32OpenZLInput::make(std::move(v)));
        }
        {
            std::vector<uint64_t> v(1000);
            for (size_t i = 0; i < v.size(); i += 2) {
                v[i]     = (uint64_t)(i % 255);
                v[i + 1] = 255 + i;
            }
            inputs.push_back(U64OpenZLInput::make(std::move(v)));
        }

        return inputs;
    }

    template <typename T>
    std::unique_ptr<OpenZLInput> makeInput(
            datagen::DataGen& gen,
            size_t inputSize) const
    {
        datagen::VectorProducer<T> dist(
                std::make_unique<datagen::LogUniformDistribution<T>>(
                        gen.getRandWrapper(),
                        1,
                        std::min<size_t>(500, std::numeric_limits<T>::max())),
                std::make_unique<datagen::ConstantDistribution<size_t>>(
                        inputSize));
        return NumericOpenZLInput<T>::make(dist("vector"));
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
            // Widths >= 2 (sentinel_byte rejects 1-byte inputs)
            const auto widthChoice = gen.usize_range("width", 0, 3);
            auto inputSize = gen.usize_range("input_size", 0, maxInputSize);
            switch (widthChoice) {
                case 0:
                    inputs.push_back(makeInput<uint8_t>(gen, inputSize));
                    break;
                case 1:
                    inputs.push_back(makeInput<uint16_t>(gen, inputSize));
                    break;
                case 2:
                    inputs.push_back(makeInput<uint32_t>(gen, inputSize));
                    break;
                case 3:
                    inputs.push_back(makeInput<uint64_t>(gen, inputSize));
                    break;
            }
        }
        return inputs;
    }

    std::vector<Benchmark> benchmarks(
            Compressor& compressor,
            datagen::DataGen& gen) const override
    {
        constexpr size_t kInputBytes = 1000000;

        std::vector<Benchmark> benchmarks;

        for (auto inputSize : { 1000, 10000, 100000 }) {
            size_t numInputs = kInputBytes / inputSize;
            std::vector<std::unique_ptr<OpenZLInput>> inputs;
            inputs.reserve(numInputs);
            for (size_t i = 0; i < numInputs; ++i) {
                inputs.push_back(makeInput<uint16_t>(gen, inputSize));
            }
            benchmarks.push_back(
                    Benchmark{
                            .name   = "InputSize:" + std::to_string(inputSize),
                            .graph  = ZL_GRAPH_COMPRESS_SMALL_LENGTHS,
                            .inputs = std::move(inputs),
                    });
        }
        return benchmarks;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makeCompressSmallLengthsComponent()
{
    return std::make_unique<CompressSmallLengthsComponent>();
}
} // namespace openzl::tests::components
