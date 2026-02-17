// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/zl_interleave.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

static std::unique_ptr<OpenZLInput> makeStrings(
        std::initializer_list<const char*> strs)
{
    std::vector<std::string> v(strs.begin(), strs.end());
    return StringOpenZLInput::make(
            poly::span<const std::string>(v.data(), v.size()));
}

class InterleaveStringComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "InterleaveString";
    }

    int minFormatVersion() const override
    {
        return 20;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { ZL_NODE_INTERLEAVE_STRING };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(makeStrings({ "hello", "world" }));
            inner.push_back(makeStrings({ "foo", "bar" }));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(makeStrings({ "" }));
            inner.push_back(makeStrings({ "" }));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(makeStrings({}));
            inner.push_back(makeStrings({}));
            inner.push_back(makeStrings({}));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // 3 inputs with multiple elements
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(makeStrings({ "a", "b", "c" }));
            inner.push_back(makeStrings({ "d", "e", "f" }));
            inner.push_back(makeStrings({ "g", "h", "i" }));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        // Varying string lengths
        {
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.push_back(makeStrings({ "short", "a" }));
            inner.push_back(makeStrings({ "much longer string here", "xy" }));
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
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
            auto numInputs  = gen.usize_range("num_inputs", 2, 4);
            auto numStrings = gen.usize_range("num_strings", 0, 20);
            std::vector<std::unique_ptr<OpenZLInput>> inner;
            inner.reserve(numInputs);
            for (size_t j = 0; j < numInputs; ++j) {
                std::string data;
                std::vector<uint32_t> lens;
                lens.reserve(numStrings);
                auto maxLen = maxInputSize
                        / (numInputs * std::max(numStrings, size_t(1)));
                for (size_t k = 0; k < numStrings; ++k) {
                    auto str = gen.randString("str", maxLen);
                    data += str;
                    lens.push_back(str.size());
                }
                inner.push_back(
                        std::make_unique<StringOpenZLInput>(
                                std::move(data), std::move(lens)));
            }
            inputs.push_back(MultiOpenZLInput::make(std::move(inner)));
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makeInterleaveStringComponent()
{
    return std::make_unique<InterleaveStringComponent>();
}
} // namespace openzl::tests::components
