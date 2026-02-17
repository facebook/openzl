// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/codecs/Prefix.hpp"
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

class PrefixComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "Prefix";
    }

    int minFormatVersion() const override
    {
        return 11;
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::Prefix{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(makeStrings({}));
        inputs.push_back(makeStrings({ "apple", "application", "apply" }));
        inputs.push_back(makeStrings({ "a", "b", "c" }));
        inputs.push_back(makeStrings({ "a", "aa", "aaa" }));
        inputs.push_back(makeStrings({ "" }));
        // Identical consecutive strings (full prefix match)
        inputs.push_back(makeStrings({ "foo", "foo", "foo" }));
        // Two empty strings
        inputs.push_back(makeStrings({ "", "" }));
        // Strings with common suffix but no prefix
        inputs.push_back(makeStrings({ "xfoo", "yfoo", "zfoo" }));
        // Long shared prefix
        {
            std::string prefix(200, 'a');
            std::vector<std::string> strs = { prefix + "x",
                                              prefix + "y",
                                              prefix + "z" };
            inputs.push_back(
                    StringOpenZLInput::make(
                            poly::span<const std::string>(
                                    strs.data(), strs.size())));
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
            auto numStrings = gen.usize_range("num_strings", 0, 100);
            std::string data;
            std::vector<uint32_t> lens;
            lens.reserve(numStrings);
            for (size_t j = 0; j < numStrings && data.size() < maxInputSize;
                 ++j) {
                auto remaining = maxInputSize - data.size();
                auto str       = gen.randString("str", remaining);
                data += str;
                lens.push_back(str.size());
            }
            inputs.push_back(
                    std::make_unique<StringOpenZLInput>(
                            std::move(data), std::move(lens)));
        }
        return inputs;
    }
};
} // namespace

std::unique_ptr<OpenZLComponent> makePrefixComponent()
{
    return std::make_unique<PrefixComponent>();
}
} // namespace openzl::tests::components
