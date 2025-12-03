// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <random>

#include "openzl/codecs/tokenize/decode_tokenize2to1_kernel.h"
#include "openzl/codecs/tokenize/decode_tokenize4to2_kernel.h"
#include "openzl/codecs/tokenize/decode_tokenizeVarto4_kernel.h"
#include "openzl/codecs/tokenize/encode_tokenize2to1_kernel.h"
#include "openzl/codecs/tokenize/encode_tokenize4to2_kernel.h"
#include "openzl/codecs/tokenize/encode_tokenizeVarto4_kernel.h"

namespace zstrong::tests {

namespace {
void roundtrip2to1(const std::vector<uint16_t>& input)
{
    std::vector<uint8_t> indexes(input.size());
    assert(indexes.data() != NULL);
    std::vector<uint16_t> alphabet(256);
    std::vector<uint16_t> regenerated(input.size());
    assert(regenerated.data() != NULL);

    size_t const alphabetSize = ZS_tokenize2to1_encode(
            indexes.data(),
            indexes.size(),
            alphabet.data(),
            alphabet.size(),
            input.data(),
            input.size());

    EXPECT_TRUE(alphabetSize <= 256);

    size_t const nbTokens = ZS_tokenize2to1_decode(
            regenerated.data(),
            regenerated.size(),
            indexes.data(),
            indexes.size(),
            alphabet.data(),
            alphabet.size());

    EXPECT_EQ(nbTokens, input.size());

    EXPECT_EQ(regenerated, input);
}
} // namespace

TEST(TokenizeKernelTest, RoundTrip2to1)
{
    std::vector<uint16_t> input = {
        0x8804, 0x4114u, 0x9cb8u, 0xc7c2u, 0xc10eu, 0xd889u, 0xcc7c, 0xbc3eu,
        0xda20, 0xffbbu, 0x14b2u, 0xf053u, 0x78dbu, 0x9bacu, 0xcef7, 0x1b09u,
        0x8804, 0x14b2u, 0x4114u, 0x78dbu, 0x9cb8u, 0x9bacu,
    };

    roundtrip2to1(input);
}

TEST(TokenizeKernelTest, RoundTrip2to1EmptyInput)
{
    std::vector<uint16_t> input = {};
    input.reserve(1); // force a non-null pointer

    roundtrip2to1(input);
}

namespace {
void roundTrip4to2(const std::vector<uint32_t>& input)
{
    std::vector<uint16_t> indexes(input.size());
    std::vector<uint32_t> alphabet(65536);
    std::vector<uint32_t> regenerated(input.size());

    size_t const alphabetSize = ZS_tokenize4to2_encode(
            indexes.data(),
            indexes.size(),
            alphabet.data(),
            alphabet.size(),
            input.data(),
            input.size(),
            ZS_tam_unsorted);

    EXPECT_TRUE(alphabetSize <= 65536);

    size_t const nbTokens = ZS_tokenize4to2_decode(
            regenerated.data(),
            regenerated.size(),
            indexes.data(),
            indexes.size(),
            alphabet.data(),
            alphabet.size());

    EXPECT_EQ(nbTokens, input.size());

    EXPECT_EQ(regenerated, input);
}
} // namespace

TEST(TokenizeKernelTest, RoundTrip4to2)
{
    std::vector<uint32_t> input = {
        0x88044114u, 0x9cb8c7c2u, 0xc10ed889u, 0xcc7cbc3eu, 0xda20ffbbu,
        0x14b2f053u, 0x78db9bacu, 0xcef71b09u, 0x88044114u, 0x9cb8c7c2u,
        0xc10ed889u, 0xcc7cbc3eu, 0xda20ffbbu, 0x14b2f053u, 0x78db9bacu,
        0xcef71b09u, 0x88044114u, 0x9cb8c7c2u, 0xc10ed889u, 0xcc7cbc3eu,
    };

    roundTrip4to2(input);
}

TEST(TokenizeKernelTest, RoundTrip4to2EmptyInput)
{
    std::vector<uint32_t> input = {};
    roundTrip4to2(input);
}

namespace {
void roundtripVarTo4(
        const std::vector<size_t>& tokenSizes,
        const std::string& inputBuffer,
        size_t totalSize,
        const size_t cardinalityEstimation)
{
    const size_t nbTokens = tokenSizes.size();
    std::vector<uint32_t> indexes(nbTokens);
    std::vector<uint8_t> alphabetBuffer(totalSize);
    std::vector<size_t> symbolSizes(nbTokens);
    std::string regenBuffer(totalSize, '0');
    std::vector<size_t> regenTokenSizes(nbTokens);

    size_t const wkspSize =
            ZS_tokenizeVarto4_encode_wkspSize(cardinalityEstimation);
    std::vector<uint8_t> wkspE(wkspSize);

    ZS_TokVar_result const tvr = ZS_tokenizeVarto4_encode(
            /* write */
            indexes.data(),
            indexes.size(),
            alphabetBuffer.data(),
            alphabetBuffer.size(),
            symbolSizes.data(),
            symbolSizes.size(),
            /* read */
            inputBuffer.data(),
            inputBuffer.size(),
            tokenSizes.data(),
            nbTokens,
            cardinalityEstimation,
            wkspE.data(),
            wkspE.size());

    EXPECT_TRUE(tvr.alphabetSize <= cardinalityEstimation);
    EXPECT_TRUE(tvr.dstSize <= totalSize);

    std::vector<uint8_t> wkspD(
            ZS_tokenizeVarto4_decode_wkspSize(tvr.alphabetSize));

    size_t const regenSize = ZS_tokenizeVarto4_decode(
            regenBuffer.data(),
            regenBuffer.size(),
            regenTokenSizes.data(),
            nbTokens,
            indexes.data(),
            nbTokens,
            alphabetBuffer.data(),
            tvr.dstSize,
            symbolSizes.data(),
            tvr.alphabetSize,
            wkspD.data(),
            wkspD.size());

    EXPECT_EQ(regenSize, totalSize);
    regenBuffer.resize(regenSize);
    EXPECT_EQ(regenBuffer, inputBuffer);
    EXPECT_EQ(regenTokenSizes, tokenSizes);
}
} // namespace

TEST(TokenizeKernelTest, RoundTripVarTo4)
{
    std::vector<size_t> tokenSizes;
    std::mt19937 gen(0xbcaa);

    size_t const maxLength = 17;
    size_t const nbTokens = std::uniform_int_distribution<size_t>(50, 150)(gen);
    size_t totalSize      = 0;
    auto dist             = std::uniform_int_distribution<size_t>(1, 17);
    for (size_t i = 0; i < nbTokens; i++) {
        size_t const size = dist(gen);
        tokenSizes.push_back(size);
        totalSize += size;
    }
    assert(tokenSizes.size() == nbTokens);

    std::string inputBuffer(totalSize, 'a'); /* init for a limited alphabet */
    roundtripVarTo4(
            tokenSizes,
            inputBuffer,
            totalSize,
            maxLength /* because content is all the same char */);
}

} // namespace zstrong::tests
