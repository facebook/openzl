// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <string>

#include "openzl/codecs/zl_generic.h"
#include "openzl/codecs/zl_parse_int.h"
#include "openzl/codecs/zl_store.h"
#include "tests/registry/OpenZLComponents.h"
#include "tests/registry/OpenZLInput.h"

namespace openzl::tests::components {
namespace {

static std::unique_ptr<OpenZLInput> makeStringList(
        std::initializer_list<const char*> strs)
{
    std::vector<std::string> v(strs.begin(), strs.end());
    return StringOpenZLInput::make(
            poly::span<const std::string>(v.data(), v.size()));
}

class TryParseIntComponent : public OpenZLComponent {
   public:
    std::string name() const override
    {
        return "TryParseInt";
    }

    int minFormatVersion() const override
    {
        // parse_int transform is version 19, but 16-bit dispatch_string
        // indices (used internally by TryParseInt) require version 21+.
        return 21;
    }

    // TryParseInt can expand short strings like "0" (1 byte) into int64_t
    // (8 bytes), plus dispatch indices overhead. Use a generous bound.
    size_t compressBound(poly::span<const Input> inputs) const override
    {
        return 10 * this->OpenZLComponent::compressBound(inputs);
    }

    std::vector<GraphID> predefinedGraphs(Compressor& compressor) const override
    {
        std::vector<GraphID> graphs;
        graphs.push_back(
                compressor.unwrap(ZL_Compressor_parameterizeTryParseIntGraph(
                        compressor.get(),
                        ZL_GRAPH_COMPRESS_GENERIC,
                        ZL_GRAPH_COMPRESS_GENERIC)));
        graphs.push_back(
                compressor.unwrap(ZL_Compressor_parameterizeTryParseIntGraph(
                        compressor.get(), ZL_GRAPH_STORE, ZL_GRAPH_STORE)));
        return graphs;
    }

    std::vector<std::unique_ptr<OpenZLInput>> predefinedInputs() const override
    {
        std::vector<std::unique_ptr<OpenZLInput>> inputs;
        // Mix of parseable and unparseable
        inputs.push_back(makeStringList({ "0", "1", "-1", "42", "hello" }));
        // All parseable
        inputs.push_back(makeStringList({ "0", "1", "2", "3" }));
        // All unparseable
        inputs.push_back(makeStringList({ "hello", "world", "foo" }));
        // Edge cases
        inputs.push_back(makeStringList(
                { "-0", "00", "+0", "+1", "-01", "0h", "0x5", "0b1" }));
        // INT64_MAX
        inputs.push_back(makeStringList({ "9223372036854775807" }));
        // INT64_MIN
        inputs.push_back(makeStringList({ "-9223372036854775808" }));
        // INT64_MAX + 1
        inputs.push_back(makeStringList({ "9223372036854775808" }));
        // INT64_MIN - 1
        inputs.push_back(makeStringList({ "-9223372036854775809" }));
        // Empty string (should fail to parse)
        inputs.push_back(makeStringList({ "" }));
        // Single parseable
        inputs.push_back(makeStringList({ "0" }));
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
            auto numStrings = gen.randLength("num_strings", maxInputSize);
            std::string data;
            std::vector<uint32_t> lens;
            lens.reserve(numStrings);
            for (size_t j = 0; j < numStrings && gen.has_more_data()
                 && data.size() < maxInputSize;
                 ++j) {
                auto isParseable = gen.boolean("is_parseable");
                std::string str;
                if (isParseable) {
                    auto val = gen.i64_range("value", INT64_MIN, INT64_MAX);
                    str      = std::to_string(val);
                } else {
                    str = gen.randString("str", maxInputSize - data.size());
                }
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

std::unique_ptr<OpenZLComponent> makeTryParseIntComponent()
{
    return std::make_unique<TryParseIntComponent>();
}
} // namespace openzl::tests::components
