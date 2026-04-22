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
        (void)compressor;
        return { ZL_NODE_LZ };
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
};
} // namespace

std::unique_ptr<OpenZLComponent> makeLzComponent()
{
    return std::make_unique<LzComponent>();
}
} // namespace openzl::tests::components
