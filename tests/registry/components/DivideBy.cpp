// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/DivideBy.hpp"
#include "openzl/shared/mem.h"
#include "openzl/zl_reflection.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {
class DivideByComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "DivideBy";
    }

    int minFormatVersion() const override
    {
        return 16;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return {
            nodes::DivideBy{}.parameterize(compressor),
            nodes::DivideBy{ 1 }.parameterize(compressor),
            nodes::DivideBy{ 2 }.parameterize(compressor),
            nodes::DivideBy{ 10 }.parameterize(compressor),
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
            auto divisor = gen.usize_range("divisor", 1, UINT64_MAX);
            result.push_back(
                    nodes::DivideBy{ divisor }.parameterize(compressor));
        }
        return result;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        // The only valid inputs for all divisors are empty inputs and zeros
        inputs.push_back(U64OpenZLInput::make(std::vector<uint64_t>{}));
        inputs.push_back(
                U64OpenZLInput::make(std::vector<uint64_t>(size_t(100), 0)));
        return inputs;
    }

    uint64_t getDivisor(const Compressor& compressor, GraphID graph) const
    {
        auto node = ZL_Compressor_Graph_getHeadNode(compressor.get(), graph);
        if (node.nid == ZL_NODE_ILLEGAL.nid) {
            throw std::runtime_error("Failed to get head node in graph");
        }
        auto params = ZL_Compressor_Node_getLocalParams(compressor.get(), node);
        if (params.copyParams.nbCopyParams == 0) {
            // Use GCD => any values are okay
            return 1;
        }
        if (params.copyParams.nbCopyParams != 1) {
            throw std::runtime_error("Failed to get divisor");
        }
        if (params.copyParams.copyParams[0].paramSize != 8) {
            throw std::runtime_error("Failed to get divisor: wrong size");
        }
        auto divisor = ZL_readLE64(params.copyParams.copyParams[0].paramPtr);
        if (divisor == 0) {
            throw std::runtime_error("Failed to get divisor: zero");
        }
        return divisor;
    }

    template <typename T>
    std::unique_ptr<OpenZLInput> generateInput(
            datagen::DataGen& gen,
            size_t maxInputSize,
            uint64_t divisor) const
    {
        auto maxElts  = maxInputSize / sizeof(T);
        auto maxValue = std::numeric_limits<T>::max() / divisor;
        auto vec      = gen.randVector<T>("values", 0, maxValue, maxElts);
        for (auto& v : vec) {
            v *= divisor;
        }
        return NumericOpenZLInput<T>::make(std::move(vec));
    }

    std::vector<std::unique_ptr<OpenZLInput>> generateInputs(
            datagen::DataGen& gen,
            size_t num,
            size_t maxInputSize,
            const Compressor& compressor,
            GraphID graph) const override
    {
        auto divisor = getDivisor(compressor, graph);
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.reserve(num);
        for (size_t i = 0; i < num; ++i) {
            auto widthChoice = gen.usize_range("width", 0, 3);
            switch (widthChoice) {
                case 0:
                    if (divisor <= std::numeric_limits<uint8_t>::max()) {
                        inputs.push_back(
                                generateInput<uint8_t>(
                                        gen, maxInputSize, divisor));
                        break;
                    }
                    ZL_FALLTHROUGH;
                case 1:
                    if (divisor <= std::numeric_limits<uint16_t>::max()) {
                        inputs.push_back(
                                generateInput<uint16_t>(
                                        gen, maxInputSize, divisor));
                        break;
                    }
                    ZL_FALLTHROUGH;
                case 2:
                    if (divisor <= std::numeric_limits<uint32_t>::max()) {
                        inputs.push_back(
                                generateInput<uint32_t>(
                                        gen, maxInputSize, divisor));
                        break;
                    }
                    ZL_FALLTHROUGH;
                case 3:
                    inputs.push_back(
                            generateInput<uint64_t>(
                                    gen, maxInputSize, divisor));
                    break;
            }
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeDivideByComponent()
{
    return std::make_unique<DivideByComponent>();
}
} // namespace openzl::tests::components
