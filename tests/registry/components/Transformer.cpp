// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <array>

#include "openzl/cpp/codecs/Transformer.hpp"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

/// Builds a numeric input of @p eltWidth bytes from @p values, truncating each
/// value to the width so one generator can feed all four widths.
std::unique_ptr<OpenZLInput> makeInputOfWidth(
        size_t eltWidth,
        const std::vector<uint64_t>& values)
{
    switch (eltWidth) {
        case 1: {
            std::vector<uint8_t> elts(values.begin(), values.end());
            return U8OpenZLInput::make(std::move(elts));
        }
        case 2: {
            std::vector<uint16_t> elts(values.begin(), values.end());
            return U16OpenZLInput::make(std::move(elts));
        }
        case 4: {
            std::vector<uint32_t> elts(values.begin(), values.end());
            return U32OpenZLInput::make(std::move(elts));
        }
        default: {
            return U64OpenZLInput::make(values);
        }
    }
}

/// The Transformer model routes on the shape of the data, so cover the shapes
/// that map onto each transform it can pick: constant, monotonic (delta),
/// GCD-divisible, narrow range, low cardinality (tokenize) and mostly-zero
/// (sparse).
std::vector<std::vector<uint64_t>> interestingValueSequences()
{
    std::vector<std::vector<uint64_t>> sequences;
    sequences.push_back({});
    sequences.push_back({ 0 });
    sequences.push_back({ 42 });
    // Constant
    sequences.push_back(std::vector<uint64_t>(200, 7));
    // Monotonically increasing with a constant stride
    {
        std::vector<uint64_t> values(200);
        for (size_t i = 0; i < values.size(); ++i) {
            values[i] = i * 3;
        }
        sequences.push_back(std::move(values));
    }
    // All values share a large common divisor
    {
        std::vector<uint64_t> values(200);
        for (size_t i = 0; i < values.size(); ++i) {
            values[i] = (i % 17) * 1000;
        }
        sequences.push_back(std::move(values));
    }
    // Narrow range far from zero
    {
        std::vector<uint64_t> values(200);
        for (size_t i = 0; i < values.size(); ++i) {
            values[i] = 100000 + (i % 5);
        }
        sequences.push_back(std::move(values));
    }
    // Low cardinality, unsorted
    {
        std::vector<uint64_t> values(200);
        for (size_t i = 0; i < values.size(); ++i) {
            values[i] = (i * 37) % 6;
        }
        sequences.push_back(std::move(values));
    }
    // Mostly zeroes with sparse literals
    {
        std::vector<uint64_t> values(200, 0);
        for (size_t i = 0; i < values.size(); i += 23) {
            values[i] = i + 1;
        }
        sequences.push_back(std::move(values));
    }
    return sequences;
}

std::unique_ptr<OpenZLInput> generateNumericInput(
        datagen::DataGen& gen,
        size_t maxInputSize)
{
    switch (gen.usize_range("width", 0, 3)) {
        case 0:
            return U8OpenZLInput::make(gen.randVector<uint8_t>(
                    "values", 0, UINT8_MAX, maxInputSize / sizeof(uint8_t)));
        case 1:
            return U16OpenZLInput::make(gen.randVector<uint16_t>(
                    "values", 0, UINT16_MAX, maxInputSize / sizeof(uint16_t)));
        case 2:
            return U32OpenZLInput::make(gen.randVector<uint32_t>(
                    "values", 0, UINT32_MAX, maxInputSize / sizeof(uint32_t)));
        default:
            return U64OpenZLInput::make(gen.randVector<uint64_t>(
                    "values", 0, UINT64_MAX, maxInputSize / sizeof(uint64_t)));
    }
}

class TransformerNumericComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "TransformerNumeric";
    }

    /// Frames with more than one input require format version 15.
    int minFormatVersion() const override
    {
        return 15;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        auto graph = graphs::TransformerNumeric{}.parameterize(compressor);
        compressor.selectStartingGraph(graph);
        return { graph };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        for (const auto& values : interestingValueSequences()) {
            for (const size_t eltWidth : std::array<size_t, 4>{ 1, 2, 4, 8 }) {
                inputs.push_back(makeInputOfWidth(eltWidth, values));
            }
        }
        // Full-width extremes, which the feature extractors treat specially.
        inputs.push_back(
                U8OpenZLInput::make(std::vector<uint8_t>{ 0, UINT8_MAX }));
        inputs.push_back(
                U16OpenZLInput::make(std::vector<uint16_t>{ 0, UINT16_MAX }));
        inputs.push_back(
                U32OpenZLInput::make(std::vector<uint32_t>{ 0, UINT32_MAX }));
        inputs.push_back(
                U64OpenZLInput::make(std::vector<uint64_t>{ 0, UINT64_MAX }));
        // Widths mixed within a single frame, since each input is routed
        // through the model independently.
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(U8OpenZLInput::make(std::vector<uint8_t>{ 1, 2 }));
            inner.push_back(
                    U32OpenZLInput::make(std::vector<uint32_t>{ 3, 4 }));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // An empty input alongside a non-empty one.
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(U64OpenZLInput::make(std::vector<uint64_t>{}));
            inner.push_back(
                    U64OpenZLInput::make(std::vector<uint64_t>{ 7, 7, 7 }));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // One input per interesting shape, all at the same width.
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            for (const auto& values : interestingValueSequences()) {
                inner.push_back(makeInputOfWidth(4, values));
            }
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
            auto numInner = gen.usize_range("num_inputs", 1, 8);
            auto innerMaxSize =
                    std::max<size_t>(maxInputSize / numInner, sizeof(uint64_t));
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.reserve(numInner);
            for (size_t j = 0; j < numInner; ++j) {
                inner.push_back(generateNumericInput(gen, innerMaxSize));
            }
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        return inputs;
    }
};

} // namespace

std::unique_ptr<OpenZLComponent> makeTransformerNumericComponent()
{
    return std::make_unique<TransformerNumericComponent>();
}

} // namespace openzl::tests::components
