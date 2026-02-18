// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Dedup.hpp"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {
class DedupNumericComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "DedupNumeric";
    }

    int minFormatVersion() const override
    {
        return 16;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::DedupNumeric{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        {
            std::vector<uint32_t> data = { 1, 2, 3 };
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(U32OpenZLInput::make(data));
            inner.push_back(U32OpenZLInput::make(data));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        {
            std::vector<uint32_t> data = { 42 };
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(U32OpenZLInput::make(data));
            inner.push_back(U32OpenZLInput::make(data));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        {
            std::vector<uint32_t> data = { 10, 20, 30, 40, 50 };
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(U32OpenZLInput::make(data));
            inner.push_back(U32OpenZLInput::make(data));
            inner.push_back(U32OpenZLInput::make(data));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // u8 identical inputs
        {
            std::vector<uint8_t> data = { 1, 2, 255 };
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(U8OpenZLInput::make(data));
            inner.push_back(U8OpenZLInput::make(data));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // u16 identical inputs
        {
            std::vector<uint16_t> data = { 100, 200, 300 };
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(U16OpenZLInput::make(data));
            inner.push_back(U16OpenZLInput::make(data));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // u64 identical inputs
        {
            std::vector<uint64_t> data = { 1, UINT64_MAX };
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(U64OpenZLInput::make(data));
            inner.push_back(U64OpenZLInput::make(data));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // 4 copies
        {
            std::vector<uint32_t> data = { 7, 8, 9 };
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(U32OpenZLInput::make(data));
            inner.push_back(U32OpenZLInput::make(data));
            inner.push_back(U32OpenZLInput::make(data));
            inner.push_back(U32OpenZLInput::make(data));
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
            auto numInputs   = gen.usize_range("num_inputs", 2, 5);
            auto widthChoice = gen.usize_range("width", 0, 3);
            std::unique_ptr<OpenZLInput> single;
            switch (widthChoice) {
                case 0: {
                    auto maxElts = maxInputSize / (numInputs * sizeof(uint8_t));
                    auto vec     = gen.randVector<uint8_t>(
                            "values", 0, UINT8_MAX, maxElts);
                    if (vec.empty())
                        vec.push_back(0);
                    single = U8OpenZLInput::make(std::move(vec));
                    break;
                }
                case 1: {
                    auto maxElts =
                            maxInputSize / (numInputs * sizeof(uint16_t));
                    auto vec = gen.randVector<uint16_t>(
                            "values", 0, UINT16_MAX, maxElts);
                    if (vec.empty())
                        vec.push_back(0);
                    single = U16OpenZLInput::make(std::move(vec));
                    break;
                }
                case 2: {
                    auto maxElts =
                            maxInputSize / (numInputs * sizeof(uint32_t));
                    auto vec = gen.randVector<uint32_t>(
                            "values", 0, UINT32_MAX, maxElts);
                    if (vec.empty())
                        vec.push_back(0);
                    single = U32OpenZLInput::make(std::move(vec));
                    break;
                }
                case 3: {
                    auto maxElts =
                            maxInputSize / (numInputs * sizeof(uint64_t));
                    auto vec = gen.randVector<uint64_t>(
                            "values", 0, UINT64_MAX, maxElts);
                    if (vec.empty())
                        vec.push_back(0);
                    single = U64OpenZLInput::make(std::move(vec));
                    break;
                }
            }
            // Serialize and deserialize to create identical copies
            auto serialized = single->serialize();
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.reserve(numInputs);
            for (size_t j = 0; j < numInputs; ++j) {
                inner.push_back(OpenZLInput::deserialize(serialized));
            }
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeDedupNumericComponent()
{
    return std::make_unique<DedupNumericComponent>();
}
} // namespace openzl::tests::components
