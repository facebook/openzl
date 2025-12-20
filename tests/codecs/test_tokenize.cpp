// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <numeric>
#include <random>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/openzl.hpp"
#include "tests/codecs/test_codec.h"
#include "tests/datagen/random_producer/compat_uniform_distribution.h"

namespace openzl {
namespace tests {
namespace {
} // namespace

class TokenizeTest : public CodecTest {
   public:
    void testTokenize(
            NodeID node,
            const Input& input,
            const Input& alphabet,
            const Input& indices,
            int minFormatVersion)
    {
        testCodec(node, input, { &alphabet, &indices }, minFormatVersion);
    }

    template <typename T, typename I>
    void testNumericTokenize(
            NodeID node,
            const std::vector<T>& input,
            const std::vector<T>& alphabet,
            const std::vector<I>& indices)
    {
        return testTokenize(
                node,
                Input::refNumeric(poly::span<const T>{ input }),
                Input::refNumeric(poly::span<const T>{ alphabet }),
                Input::refNumeric(poly::span<const I>{ indices }),
                ZL_TYPED_INPUT_VERSION_MIN);
    }

    template <typename T, typename I>
    void testStructTokenize(
            NodeID node,
            const std::vector<T>& input,
            const std::vector<T>& alphabet,
            const std::vector<I>& indices)
    {
        return testTokenize(
                node,
                Input::refStruct(poly::span<const T>{ input }),
                Input::refStruct(poly::span<const T>{ alphabet }),
                Input::refNumeric(poly::span<const I>{ indices }),
                ZL_TYPED_INPUT_VERSION_MIN);
    }

    std::pair<std::string, std::vector<uint32_t>> toContentLengths(
            const std::vector<std::string>& input)
    {
        std::string content;
        std::vector<uint32_t> lengths;
        lengths.reserve(input.size());
        for (const auto& in : input) {
            content.append(in);
            lengths.push_back(in.size());
        }
        return std::make_pair(std::move(content), std::move(lengths));
    }

    template <typename I>
    void testStringTokenize(
            NodeID node,
            const std::vector<std::string>& input,
            const std::vector<std::string>& alphabet,
            const std::vector<I>& indices)
    {
        auto [iContent, iLen] = toContentLengths(input);
        auto [aContent, aLen] = toContentLengths(alphabet);
        return testTokenize(
                node,
                Input::refString(iContent, iLen),
                Input::refString(aContent, aLen),
                Input::refNumeric(poly::span<const I>(indices)),
                ZL_TYPED_INPUT_VERSION_MIN);
    }
};

TEST_F(TokenizeTest, TestWorksAsExpected)
{
    try {
        testNumericTokenize(
                nodes::TokenizeNumeric().parameterize(compressor_),
                std::vector<uint8_t>{ 5, 4 },
                std::vector<uint8_t>{},
                std::vector<uint8_t>{});
        ASSERT_TRUE(false) << "Must throw";
    } catch (const Exception& e) {
        ASSERT_NE(
                std::string(e.what()).find("Input does not match expectations"),
                std::string::npos);
    }
}

TEST_F(TokenizeTest, TokenizeNumericUnsorted)
{
    testNumericTokenize(
            nodes::TokenizeNumeric().parameterize(compressor_),
            std::vector<uint8_t>{ 5, 4, 5, 1, 1 },
            std::vector<uint8_t>{ 5, 4, 1 },
            std::vector<uint8_t>{ 0, 1, 0, 2, 2 });
    testNumericTokenize(
            nodes::TokenizeNumeric(false).parameterize(compressor_),
            std::vector<int16_t>{ 5, 4, 5, 1, 1 },
            std::vector<int16_t>{ 5, 4, 1 },
            std::vector<uint8_t>{ 0, 1, 0, 2, 2 });
    testNumericTokenize(
            nodes::TokenizeNumeric().parameterize(compressor_),
            std::vector<int32_t>{ 5, 4, 5, 1, 1 },
            std::vector<int32_t>{ 5, 4, 1 },
            std::vector<uint8_t>{ 0, 1, 0, 2, 2 });
    testNumericTokenize(
            nodes::TokenizeNumeric().parameterize(compressor_),
            std::vector<int64_t>{ 5, 4, 5, 1, 1 },
            std::vector<int64_t>{ 5, 4, 1 },
            std::vector<uint8_t>{ 0, 1, 0, 2, 2 });

    std::vector<int> data(300);
    std::iota(data.begin(), data.end(), 0);

    std::vector<uint16_t> indices(300);
    std::iota(indices.begin(), indices.end(), 0);

    testNumericTokenize(
            nodes::TokenizeNumeric().parameterize(compressor_),
            data,
            data,
            indices);
}

TEST_F(TokenizeTest, TokenizeNumericSorted)
{
    testNumericTokenize(
            nodes::TokenizeNumeric(true).parameterize(compressor_),
            std::vector<uint8_t>{ 5, 4, 5, 1, 1 },
            std::vector<uint8_t>{ 1, 4, 5 },
            std::vector<uint8_t>{ 2, 1, 2, 0, 0 });
    testNumericTokenize(
            nodes::TokenizeNumeric(true).parameterize(compressor_),
            std::vector<uint16_t>{ 5, 4, 5, 1, 1 },
            std::vector<uint16_t>{ 1, 4, 5 },
            std::vector<uint8_t>{ 2, 1, 2, 0, 0 });
    testNumericTokenize(
            nodes::TokenizeNumeric(true).parameterize(compressor_),
            std::vector<uint32_t>{ 5, 4, 5, 1, 1 },
            std::vector<uint32_t>{ 1, 4, 5 },
            std::vector<uint8_t>{ 2, 1, 2, 0, 0 });
    testNumericTokenize(
            nodes::TokenizeNumeric(true).parameterize(compressor_),
            std::vector<uint64_t>{ 5, 4, 5, 1, 1 },
            std::vector<uint64_t>{ 1, 4, 5 },
            std::vector<uint8_t>{ 2, 1, 2, 0, 0 });

    std::vector<int> data(300);
    std::iota(data.begin(), data.end(), 0);

    std::vector<uint16_t> indices(300);
    std::iota(indices.begin(), indices.end(), 0);

    testNumericTokenize(
            nodes::TokenizeNumeric(true).parameterize(compressor_),
            data,
            data,
            indices);
}

TEST_F(TokenizeTest, TokenizeStruct)
{
    testStructTokenize(
            nodes::TokenizeStruct().parameterize(compressor_),
            std::vector<uint8_t>{ 5, 4, 5, 1, 1 },
            std::vector<uint8_t>{ 5, 4, 1 },
            std::vector<uint8_t>{ 0, 1, 0, 2, 2 });
    testStructTokenize(
            nodes::TokenizeStruct().parameterize(compressor_),
            std::vector<int16_t>{ 5, 4, 5, 1, 1 },
            std::vector<int16_t>{ 5, 4, 1 },
            std::vector<uint8_t>{ 0, 1, 0, 2, 2 });
    testStructTokenize(
            nodes::TokenizeStruct().parameterize(compressor_),
            std::vector<int32_t>{ 5, 4, 5, 1, 1 },
            std::vector<int32_t>{ 5, 4, 1 },
            std::vector<uint8_t>{ 0, 1, 0, 2, 2 });
    testStructTokenize(
            nodes::TokenizeStruct().parameterize(compressor_),
            std::vector<int64_t>{ 5, 4, 5, 1, 1 },
            std::vector<int64_t>{ 5, 4, 1 },
            std::vector<uint8_t>{ 0, 1, 0, 2, 2 });

    std::vector<int> data(300);
    std::iota(data.begin(), data.end(), 0);

    std::vector<uint16_t> indices(300);
    std::iota(indices.begin(), indices.end(), 0);

    testStructTokenize(
            nodes::TokenizeStruct().parameterize(compressor_),
            data,
            data,
            indices);
}

TEST_F(TokenizeTest, TokenizeStringUnsorted)
{
    testStringTokenize(
            nodes::TokenizeString().parameterize(compressor_),
            { "zstd", "hello", "world", "hello", "me", "zstd" },
            { "zstd", "hello", "world", "me" },
            std::vector<uint8_t>{ 0, 1, 2, 1, 3, 0 });
    testStringTokenize(
            nodes::TokenizeString(false).parameterize(compressor_),
            { "zstd", "hello", "world", "hello", "me", "zstd" },
            { "zstd", "hello", "world", "me" },
            std::vector<uint8_t>{ 0, 1, 2, 1, 3, 0 });
}

TEST_F(TokenizeTest, TokenizeStringSorted)
{
    testStringTokenize(
            nodes::TokenizeString(true).parameterize(compressor_),
            { "zstd", "hello", "world", "hello", "me", "zstd" },
            { "hello", "me", "world", "zstd" },
            std::vector<uint8_t>{ 3, 0, 2, 0, 1, 3 });

    datagen::compat_uniform_int_distribution<uint8_t> chrDist('a', 'z');
    std::uniform_int_distribution<size_t> lenDist(0, 5);
    std::mt19937 gen(0xdeadbeef);

    std::set<std::string> keys;
    while (keys.size() < 500) {
        std::string key;
        auto len = lenDist(gen);
        for (size_t i = 0; i < len; ++i) {
            key.push_back(char(chrDist(gen)));
        }
        keys.insert(key);
    }

    std::vector<std::string> alphabet;
    for (const auto& key : keys) {
        alphabet.push_back(key);
    }

    std::vector<std::string> input;
    std::vector<uint16_t> indices;
    std::uniform_int_distribution<uint16_t> idxDist(0, alphabet.size() - 1);
    for (size_t i = 0; i < 2000; ++i) {
        auto idx = idxDist(gen);
        input.push_back(alphabet[idx]);
        indices.push_back(idx);
    }
    // Ensure every symbol in the alphabet is present
    for (uint16_t i = 0; i < alphabet.size(); ++i) {
        input.push_back(alphabet[i]);
        indices.push_back(i);
    }

    testStringTokenize(
            nodes::TokenizeString(true).parameterize(compressor_),
            input,
            alphabet,
            indices);
}

} // namespace tests
} // namespace openzl
