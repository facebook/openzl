// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <random>

#include "security/lionhead/utils/lib_ftest/ftest.h"

#include "custom_transforms/parse/decode_parse.h"
#include "custom_transforms/parse/tests/parse_test_data.h"
#include "openzl/common/assertion.h"
#include "tests/fuzz_utils.h"

using namespace ::testing;

namespace openzl::tests::parse {
namespace {
template <Type kType>
std::vector<std::string> const& fieldExamples()
{
    static auto* examplesPtr =
            new std::vector<std::string>([] { return genData(256, kType); }());
    return *examplesPtr;
}

template <Type kType>
std::vector<std::vector<std::string>> const& compressExamples()
{
    static auto* examplesPtr = new std::vector<std::vector<std::string>>([] {
        std::vector<std::vector<std::string>> examples;
        std::mt19937 gen(0xdeadbeef);
        for (size_t n = 0; n < 40; ++n) {
            examples.push_back(genData(gen, n * 100, kType));
        }
        return examples;
    }());
    return *examplesPtr;
}

template <Type kType>
std::vector<std::string> const& decompressExamples()
{
    static auto* examplesPtr = new std::vector<std::string>([] {
        std::vector<std::string> examples;
        for (auto const& example : compressExamples<kType>()) {
            examples.push_back(compress(example, kType));
        }
        return examples;
    }());
    return *examplesPtr;
}
} // namespace

FUZZ(ParseTest, FuzzInt64RoundTrip)
{
    auto const data =
            f.d_vec(f.d_str().with_examples(fieldExamples<Type::Int64>()))
                    .gen("input_data", f);

    auto const compressed   = compress(data, Type::Int64);
    auto const decompressed = decompress(compressed, Type::Int64);
    ZL_REQUIRE(decompressed == flatten(data).first);
}

FUZZ(ParseTest, FuzzInt64Decompress)
{
    std::string input =
            gen_str(f,
                    "input_data",
                    InputLengthInBytes(1),
                    decompressExamples<Type::Int64>());
    try {
        size_t const maxDstSize =
                std::min<size_t>(10 << 20, input.size() * 100);
        decompress(input, Type::Int64, maxDstSize);
    } catch (...) {
        // Failure is okay, just cannot crash
    }
}

FUZZ(ParseTest, FuzzFloat64RoundTrip)
{
    auto const data =
            f.d_vec(f.d_str().with_examples(fieldExamples<Type::Float64>()))
                    .gen("input_data", f);
    auto const compressed   = compress(data, Type::Float64);
    auto const decompressed = decompress(compressed, Type::Float64);
    ZL_REQUIRE(decompressed == flatten(data).first);
}

FUZZ(ParseTest, FuzzFloat64Decompress)
{
    std::string input =
            gen_str(f,
                    "input_data",
                    InputLengthInBytes(1),
                    decompressExamples<Type::Float64>());
    try {
        size_t const maxDstSize =
                std::min<size_t>(10 << 20, input.size() * 100);
        decompress(input, Type::Float64, maxDstSize);
    } catch (...) {
        // Failure is okay, just cannot crash
    }
}

} // namespace openzl::tests::parse
