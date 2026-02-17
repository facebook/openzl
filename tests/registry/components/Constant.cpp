// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Constant.hpp"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {
class ConstantComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Constant";
    }

    int minFormatVersion() const override
    {
        return 11;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        auto graph = graphs::Constant{}.parameterize(compressor);
        compressor.selectStartingGraph(graph);
        return { graph };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        // Serial inputs (constant byte repeated)
        inputs.push_back(SerialOpenZLInput::make("x"));
        inputs.push_back(SerialOpenZLInput::make("aaaa"));
        inputs.push_back(SerialOpenZLInput::make(std::string(100, '\x00')));
        inputs.push_back(SerialOpenZLInput::make(std::string(50, '\xFF')));
        // Struct inputs
        // width=1, 3 identical elements 'a'
        inputs.push_back(StructOpenZLInput::make("aaa", 1));
        // width=2, 2 identical elements 'ab'
        inputs.push_back(StructOpenZLInput::make("abab", 2));
        // width=4, 5 identical elements 'abcd'
        inputs.push_back(StructOpenZLInput::make("abcdabcdabcd", 4));
        // width=8, 3 identical elements 'abcdefgh'
        inputs.push_back(
                StructOpenZLInput::make("abcdefghabcdefghabcdefgh", 8));
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
            bool useSerial = gen.boolean("use_serial");
            if (useSerial) {
                // At least 1 byte (empty not valid for Constant)
                auto numBytes = gen.usize_range("num_bytes", 1, maxInputSize);
                char byte     = gen.i8_range("byte", -128, 127);
                inputs.push_back(
                        SerialOpenZLInput::make(std::string(numBytes, byte)));
            } else {
                auto width   = gen.usize_range("width", 1, 10);
                auto maxElts = std::max<size_t>(maxInputSize / width, 1);
                // At least 1 element (empty not valid for Constant)
                auto numElts = gen.usize_range("num_elts", 1, maxElts);
                // Generate one random element and replicate it
                std::string elt;
                for (size_t j = 0; j < width; ++j) {
                    elt += gen.i8_range("elt", -128, 127);
                }
                std::string data;
                data.reserve(numElts * width);
                for (size_t j = 0; j < numElts; ++j) {
                    data += elt;
                }
                inputs.push_back(
                        StructOpenZLInput::make(std::move(data), width));
            }
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeConstantComponent()
{
    return std::make_unique<ConstantComponent>();
}
} // namespace openzl::tests::components
