// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Flatpack.hpp"
#include "tests/datagen/structures/CompressibleStringProducer.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"
#include "tests/utils.h"

namespace openzl::tests::components {
namespace {
class FlatpackComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Flatpack";
    }

    int minFormatVersion() const override
    {
        return 3;
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        auto graph = graphs::Flatpack{}.parameterize(compressor);
        compressor.selectStartingGraph(graph);
        return { graph };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(SerialOpenZLInput::make(""));
        inputs.push_back(SerialOpenZLInput::make("x"));
        inputs.push_back(SerialOpenZLInput::make(std::string(100, 'a')));
        inputs.push_back(SerialOpenZLInput::make(kLoremTestInput));
        // All 256 unique byte values
        {
            std::string allBytes;
            for (int b = 0; b < 256; ++b) {
                allBytes.push_back(static_cast<char>(b));
            }
            inputs.push_back(SerialOpenZLInput::make(std::move(allBytes)));
        }
        // Two-byte input
        inputs.push_back(SerialOpenZLInput::make("ab"));
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
            inputs.push_back(
                    SerialOpenZLInput::make(
                            gen.randString("data", maxInputSize)));
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeFlatpackComponent()
{
    return std::make_unique<FlatpackComponent>();
}
} // namespace openzl::tests::components
