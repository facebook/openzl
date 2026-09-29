// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/MuxLengths.hpp"
#include "openzl/cpp/codecs/Store.hpp"
#include "openzl/zl_reflection.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

/// Create a multi-input with two numeric vectors of the same type.
template <typename T>
std::unique_ptr<OpenZLInput> makePairInput(
        std::vector<T> literalLengths,
        std::vector<T> matchLengths)
{
    std::vector<std::unique_ptr<OpenZLInput>> inputs;
    inputs.push_back(NumericOpenZLInput<T>::make(std::move(literalLengths)));
    inputs.push_back(NumericOpenZLInput<T>::make(std::move(matchLengths)));
    return MultiOpenZLInput::make(std::move(inputs));
}

class MuxLengthsComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "MuxLengths";
    }

    int minFormatVersion() const override
    {
        return 24;
    }

    // Muxing can expand: 1 byte per pair + overflow stream.
    size_t compressBound(poly::span<const Input> inputs) const override
    {
        return 3 * this->OpenZLComponent::compressBound(inputs);
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        return {
            nodes::MuxLengths{ 3, 3 }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
            nodes::MuxLengths{ 0, 0 }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
            nodes::MuxLengths{ 8, 0 }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
            nodes::MuxLengths{ 4, 0 }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE),
        };
    }

    std::vector<GraphID> generateGraphs(
            Compressor& compressor,
            datagen::DataGen& gen,
            size_t num) const override
    {
        std::vector<GraphID> result;
        result.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            unsigned sp = (unsigned)gen.usize_range("split_point", 0, 8);
            unsigned mlb =
                    (unsigned)gen.usize_range("match_length_bias", 0, 15);
            result.push_back(
                    nodes::MuxLengths{ sp, mlb }(
                            compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE));
        }
        return result;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        // Predefined inputs must work for ALL graph configurations, including
        // generated ones with match_length_bias up to 15. So all match lengths
        // must be >= 15.
        std::vector<std::unique_ptr<OpenZLInput>> inputs;

        // Empty inputs
        inputs.push_back(
                makePairInput<uint32_t>(
                        std::vector<uint32_t>{}, std::vector<uint32_t>{}));

        // Single element
        inputs.push_back(
                makePairInput<uint32_t>(
                        std::vector<uint32_t>{ 0 },
                        std::vector<uint32_t>{ 15 }));

        // Mixed inline and overflow values
        inputs.push_back(
                makePairInput<uint32_t>(
                        std::vector<uint32_t>{ 0, 1, 7, 255, 1000 },
                        std::vector<uint32_t>{ 15, 16, 20, 100, 15 }));

        // All zeros for literal lengths
        inputs.push_back(
                makePairInput<uint32_t>(
                        std::vector<uint32_t>{ 0, 0, 0 },
                        std::vector<uint32_t>{ 15, 15, 15 }));

        // u8 width
        inputs.push_back(
                makePairInput<uint8_t>(
                        std::vector<uint8_t>{ 0, 1, 7, 255 },
                        std::vector<uint8_t>{ 15, 16, 30, 100 }));

        // u16 width
        inputs.push_back(
                makePairInput<uint16_t>(
                        std::vector<uint16_t>{ 0, 1, 100, 65535 },
                        std::vector<uint16_t>{ 15, 16, 1000, 65535 }));

        // u64 width
        inputs.push_back(
                makePairInput<uint64_t>(
                        std::vector<uint64_t>{ 0, 1, 1000 },
                        std::vector<uint64_t>{ 15, 16, 1000 }));

        return inputs;
    }

    template <typename T>
    std::unique_ptr<OpenZLInput> makeInput(
            datagen::DataGen& gen,
            size_t inputSize,
            unsigned splitPoint,
            T matchLengthBias,
            float llSmallProb,
            float mlSmallProb) const
    {
        std::vector<T> lls;
        std::vector<T> mls;
        lls.reserve(inputSize);
        mls.reserve(inputSize);

        auto genValue = [&gen](T min, int smallBits, float smallProb) {
            const T threshold = smallBits == 8 && sizeof(T) == 1
                    ? 255
                    : min + (T(1) << smallBits) - 1;
            if (smallBits > 0 && gen.coin("small", smallProb)) {
                return gen.getRandWrapper()->range(
                        "small_val", min, T(threshold - 1));
            } else {
                return gen.getRandWrapper()->range(
                        "large_val", threshold, std::numeric_limits<T>::max());
            }
        };
        const int llBits = splitPoint;
        const int mlBits = 8 - splitPoint;
        for (size_t i = 0; i < inputSize; ++i) {
            lls.push_back(genValue(0, llBits, llSmallProb));
            mls.push_back(genValue(matchLengthBias, mlBits, mlSmallProb));
        }
        return makePairInput(std::move(lls), std::move(mls));
    }

    std::pair<unsigned, unsigned> getSplitPointAndBias(
            const Compressor& compressor,
            GraphID graphID) const
    {
        // Extract match_length_bias from the graph's parameters.
        auto headNode =
                ZL_Compressor_Graph_getHeadNode(compressor.get(), graphID);
        if (headNode == ZL_NODE_ILLEGAL) {
            throw std::runtime_error("Invalid graph ID");
        }
        auto localParams =
                ZL_Compressor_Node_getLocalParams(compressor.get(), headNode);
        unsigned splitPoint      = 4;
        unsigned matchLengthBias = 0;
        for (size_t i = 0; i < localParams.intParams.nbIntParams; ++i) {
            if (localParams.intParams.intParams[i].paramId
                == ZL_MUX_LENGTHS_MATCH_LENGTH_BIAS_PID) {
                matchLengthBias =
                        (unsigned)localParams.intParams.intParams[i].paramValue;
            }
            if (localParams.intParams.intParams[i].paramId
                == ZL_MUX_LENGTHS_SPLIT_POINT_PID) {
                splitPoint =
                        (unsigned)localParams.intParams.intParams[i].paramValue;
            }
        }
        if (splitPoint > 8) {
            throw std::runtime_error("bad split point");
        }
        if (matchLengthBias > 15) {
            throw std::runtime_error("bad match length bias");
        }
        return { splitPoint, matchLengthBias };
    }

    std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSizeBytes,
            const Compressor& compressor,
            GraphID graphID) const override
    {
        auto [splitPoint, matchLengthBias] =
                getSplitPointAndBias(compressor, graphID);
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            auto widthChoice = gen.usize_range("width", 0, 3);
            // Divide by element width to get the max number of elements.
            // Note that there are two input streams to mux lengths.
            auto maxInputSize = maxInputSizeBytes / (2 * (1u << widthChoice));
            auto inputSize    = gen.usize_range("inputSize", 0, maxInputSize);
            auto llSmallProb  = gen.f32_range("llSmallProb", 0, 1);
            auto mlSmallProb  = gen.f32_range("mlSmallProb", 0, 1);
            switch (widthChoice) {
                case 0:
                    inputs.push_back(
                            makeInput<uint8_t>(
                                    gen,
                                    inputSize,
                                    splitPoint,
                                    (uint8_t)matchLengthBias,
                                    llSmallProb,
                                    mlSmallProb));
                    break;
                case 1:
                    inputs.push_back(
                            makeInput<uint16_t>(
                                    gen,
                                    inputSize,
                                    splitPoint,
                                    (uint16_t)matchLengthBias,
                                    llSmallProb,
                                    mlSmallProb));
                    break;
                case 2:
                    inputs.push_back(
                            makeInput<uint32_t>(
                                    gen,
                                    inputSize,
                                    splitPoint,
                                    (uint32_t)matchLengthBias,
                                    llSmallProb,
                                    mlSmallProb));
                    break;
                case 3:
                    inputs.push_back(
                            makeInput<uint64_t>(
                                    gen,
                                    inputSize,
                                    splitPoint,
                                    (uint64_t)matchLengthBias,
                                    llSmallProb,
                                    mlSmallProb));
                    break;
            }
        }
        return inputs;
    }

    std::vector<Benchmark> benchmarks(
            Compressor& compressor,
            datagen::DataGen& gen) const override
    {
        const size_t kInputSize                                        = 10000;
        constexpr std::array<poly::optional<unsigned>, 2> kSplitPoints = {
            poly::nullopt, 4
        };
        std::vector<Benchmark> benchmarks;
        benchmarks.reserve(kSplitPoints.size());
        for (auto splitPoint : kSplitPoints) {
            auto graph = nodes::MuxLengths{ splitPoint }(
                    compressor, ZL_GRAPH_STORE, ZL_GRAPH_STORE);

            std::vector<std::unique_ptr<OpenZLInput>> inputs;
            for (const size_t smallProb : { 95, 90, 75, 50 }) {
                inputs.push_back(
                        makeInput<uint16_t>(
                                gen,
                                kInputSize,
                                4,
                                0,
                                smallProb / 100.0,
                                smallProb / 100.0));
            }

            benchmarks.push_back(
                    Benchmark{
                            .name = "SplitPoint:"
                                    + (splitPoint.has_value()
                                               ? std::to_string(*splitPoint)
                                               : "null")
                                    + "/InputSize:"
                                    + std::to_string(kInputSize),
                            .graph  = graph,
                            .inputs = std::move(inputs),
                    });
        }
        return benchmarks;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makeMuxLengthsComponent()
{
    return std::make_unique<MuxLengthsComponent>();
}
} // namespace openzl::tests::components
