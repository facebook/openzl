// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Transpose.hpp"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {
class TransposeComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Transpose";
    }

    int minFormatVersion() const override
    {
        return 11;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::TransposeSplit{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        // Empty struct (width 2)
        inputs.push_back(StructOpenZLInput::make("", 2));
        // Single element struct (width 4)
        inputs.push_back(StructOpenZLInput::make(std::string(4, '\x01'), 4));
        // 4 elements of width 2
        inputs.push_back(
                StructOpenZLInput::make("\x01\x02\x03\x04\x05\x06\x07\x08", 2));
        // 2 elements of width 4
        inputs.push_back(
                StructOpenZLInput::make("\x01\x02\x03\x04\x05\x06\x07\x08", 4));
        // Width 1 (trivial, single stream)
        inputs.push_back(StructOpenZLInput::make("\x01\x02\x03\x04", 1));
        // Width 3 (small non-power-of-2)
        inputs.push_back(
                StructOpenZLInput::make(
                        "\x01\x02\x03\x04\x05\x06\x07\x08\x09", 3));
        // Width 8 with varying byte values
        {
            std::string data;
            for (int i = 0; i < 16; ++i) {
                data.push_back(static_cast<char>(i * 17));
            }
            inputs.push_back(StructOpenZLInput::make(std::move(data), 8));
        }
        // Width 16
        inputs.push_back(StructOpenZLInput::make(std::string(32, '\xCD'), 16));
        // Width 32
        inputs.push_back(StructOpenZLInput::make(std::string(64, '\xEF'), 32));
        // Width 64
        inputs.push_back(StructOpenZLInput::make(std::string(128, '\x42'), 64));
        // Width 11 (not a power of 2)
        inputs.push_back(StructOpenZLInput::make(std::string(33, '\x99'), 11));
        // Large number of elements (100 elements of width 2)
        inputs.push_back(StructOpenZLInput::make(std::string(200, '\xAB'), 2));
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
            auto width = gen.usize_range("width", 1, 100);
            auto data  = gen.randStringWithQuantizedLength(
                    "data", maxInputSize, width);
            inputs.push_back(StructOpenZLInput::make(std::move(data), width));
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeTransposeComponent()
{
    return std::make_unique<TransposeComponent>();
}
} // namespace openzl::tests::components
