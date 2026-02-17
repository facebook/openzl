// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <string>

#include "openzl/cpp/codecs/ParseInt.hpp"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

static std::unique_ptr<OpenZLInput> makeIntStrings(
        std::initializer_list<const char*> strs)
{
    std::vector<std::string> v(strs.begin(), strs.end());
    return StringOpenZLInput::make(
            poly::span<const std::string>(v.data(), v.size()));
}

class ParseIntComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "ParseInt";
    }

    int minFormatVersion() const override
    {
        return 19;
    }

    // ParseInt converts each string to an int64_t (8 bytes), so short strings
    // like "0" (1 byte) expand to 8 bytes. Use a generous bound.
    size_t compressBound(poly::span<const Input> inputs) const override
    {
        size_t totalSrcSize = 0;
        for (const auto& input : inputs) {
            totalSrcSize += input.contentSize();
        }
        return ZL_compressBound(8 * totalSrcSize);
    }

    std::vector<NodeID> predefinedNodes(Compressor& compressor) const override
    {
        return { nodes::ParseInt{}.parameterize(compressor) };
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        inputs.push_back(makeIntStrings({}));
        inputs.push_back(makeIntStrings({ "0" }));
        inputs.push_back(makeIntStrings({ "1", "2", "3" }));
        inputs.push_back(makeIntStrings({ "-1", "0", "1" }));
        inputs.push_back(makeIntStrings({ "123456789", "-987654321" }));
        // INT64_MAX and INT64_MIN
        inputs.push_back(makeIntStrings(
                { "9223372036854775807", "-9223372036854775808" }));
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
                auto val = gen.i64_range("value", INT64_MIN, INT64_MAX);
                auto str = std::to_string(val);
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

std::unique_ptr<OpenZLComponent> makeParseIntComponent()
{
    return std::make_unique<ParseIntComponent>();
}
} // namespace openzl::tests::components
