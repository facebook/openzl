// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <random>

#include <gtest/gtest.h>

#include "openzl/codecs/entropy/deprecated/common_entropy.h"
#include "openzl/codecs/entropy/deprecated/decode_fse_kernel.h"
#include "openzl/codecs/entropy/deprecated/encode_fse_kernel.h"
#include "tests/utils.h"

namespace openzl {
namespace tests {
namespace {

void testRoundTrip(const std::string& input)
{
    auto compressedSizeBound = ZS_Entropy_encodedSizeBound(input.size(), 1);
    std::string compressed;
    compressed.resize(compressedSizeBound + 1);
    auto compressedWC = ZL_WC_wrap((uint8_t*)&compressed[0], compressed.size());

    EXPECT_FALSE(ZL_isError(ZS_Entropy_encodeFse(
            &compressedWC, input.data(), input.size(), 1, 0)));
    compressed.resize(ZL_WC_size(&compressedWC));
    auto compressedRC = ZL_RC_wrapWC(&compressedWC);

    std::string output;
    output.resize(input.size() + 1);

    ZL_Report const ret = ZS_Entropy_decodeDefault(
            output.data(), output.size(), &compressedRC, 1);
    EXPECT_FALSE(ZL_isError(ret));
    EXPECT_EQ(ZL_RC_avail(&compressedRC), 0u);
    output.resize(ZL_validResult(ret));

    EXPECT_EQ(output, input);
}

TEST(FSETest, testEmptyRoundTrip)
{
    testRoundTrip(kEmptyTestInput);
}

TEST(FSETest, testFooRoundTrip)
{
    testRoundTrip(kFooTestInput);
}

TEST(FSETest, testLoremRoundTrip)
{
    testRoundTrip(kLoremTestInput);
}

TEST(FSETest, testAudioRoundTrip)
{
    testRoundTrip(kAudioPCMS32LETestInput);
}

TEST(FSETest, testConstantRoundTrip)
{
    std::string input;
    input.resize(1000, '\xAB');
    testRoundTrip(input);
}

TEST(FSETest, testUncompressibleRoundTrip)
{
    std::string input;
    input.resize(2560);
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = (char)i;
    }
    testRoundTrip(input);
}

static uint8_t mixExpectEq(void* opaque, uint8_t ctx, uint8_t o1)
{
    EXPECT_EQ(nullptr, opaque);
    EXPECT_EQ(ctx, o1);
    return ctx;
}

void testRoundTripContext(const std::string& input)
{
    ZL_RC ctx;
    ZL_RC src;
    if (input.empty()) {
        ctx = ZL_RC_makeEmpty();
        src = ZL_RC_makeEmpty();
    } else {
        ctx = ZL_RC_wrap((const uint8_t*)input.data(), input.size() - 1);
        src = ZL_RC_wrap((const uint8_t*)input.data() + 1, input.size() - 1);
    }
    // TODO: bound this. Make the compression code handle out of space and emit
    // literally.
    size_t const compressedSizeBound = 2 * input.size() + 1000;
    std::string compressed;
    compressed.resize(compressedSizeBound + 1);

    ZL_ContextClustering clustering;
    ZL_ContextClustering_identity(&clustering, ctx);

    {
        auto compressedWC =
                ZL_WC_wrap((uint8_t*)&compressed[0], compressed.size());
        ZL_RC ctxCpy = ctx;
        EXPECT_ZS_VALID(
                ZS_fseContextEncode(&compressedWC, &src, &ctxCpy, &clustering));
        EXPECT_EQ(ZL_RC_avail(&ctxCpy), 0u);
        EXPECT_EQ(ZL_RC_avail(&src), 0u);
        auto compressedRC = ZL_RC_wrapWC(&compressedWC);

        std::string output;
        output.resize(input.size() + 1);
        auto outputWC = ZL_WC_wrap((uint8_t*)&output[0], output.size());

        ctxCpy = ctx;
        EXPECT_ZS_VALID(ZS_fseContextDecode(&outputWC, &compressedRC, &ctxCpy));
        EXPECT_EQ(ZL_RC_avail(&compressedRC), 0u);
        output.resize(ZL_WC_size(&outputWC));

        EXPECT_EQ(output, input.empty() ? input : input.substr(1));
    }

    {
        auto compressedWC =
                ZL_WC_wrap((uint8_t*)&compressed[0], compressed.size());
        src          = ZL_RC_wrap((const uint8_t*)input.data(), input.size());
        auto context = " " + input;
        ctx = ZL_RC_wrap((const uint8_t*)context.data(), context.size() - 1);
        EXPECT_ZS_VALID(ZS_fseContextO1Encode(
                &compressedWC, &src, &ctx, mixExpectEq, nullptr, &clustering));
        EXPECT_EQ(ZL_RC_avail(&ctx), 0u);
        EXPECT_EQ(ZL_RC_avail(&src), 0u);
        auto compressedRC = ZL_RC_wrapWC(&compressedWC);

        std::string output;
        output.resize(input.size() + 1);
        auto outputWC = ZL_WC_wrap((uint8_t*)&output[0], output.size());

        ctx = ZL_RC_wrap((const uint8_t*)context.data(), context.size() - 1);
        EXPECT_ZS_VALID(ZS_fseContextO1Decode(
                &outputWC, &compressedRC, &ctx, mixExpectEq, nullptr));
        EXPECT_EQ(ZL_RC_avail(&compressedRC), 0u);
        output.resize(ZL_WC_size(&outputWC));

        EXPECT_EQ(output, input);
    }

    {
        src = ZL_RC_wrap((const uint8_t*)input.data(), input.size());
        auto compressedWC =
                ZL_WC_wrap((uint8_t*)&compressed[0], compressed.size());
        ZL_RC cpy = src;
        EXPECT_ZS_VALID(ZS_fseO1Encode(&compressedWC, &cpy, &clustering));
        EXPECT_EQ(ZL_RC_avail(&cpy), 0u);
        auto compressedRC = ZL_RC_wrapWC(&compressedWC);

        std::string output;
        output.resize(input.size() + 1);
        auto outputWC = ZL_WC_wrap((uint8_t*)&output[0], output.size());

        EXPECT_ZS_VALID(ZS_fseO1Decode(&outputWC, &compressedRC));
        EXPECT_EQ(ZL_RC_avail(&compressedRC), 0u);
        output.resize(ZL_WC_size(&outputWC));

        EXPECT_EQ(output, input);
    }
}

TEST(FSEContextTest, testEmptyRoundTrip)
{
    testRoundTripContext(kEmptyTestInput);
}

TEST(FSEContextTest, testFooRoundTrip)
{
    testRoundTripContext(kFooTestInput);
}

TEST(FSEContextTest, testLoremRoundTrip)
{
    testRoundTripContext(kLoremTestInput);
}

TEST(FSEContextTest, testAudioRoundTrip)
{
    testRoundTripContext(kAudioPCMS32LETestInput);
}

TEST(FSEContextTest, testConstantRoundTrip)
{
    std::string input;
    input.resize(1000, '\xAB');
    testRoundTripContext(input);
}

TEST(FSEContextTest, testO1ConstantRoundTrip)
{
    std::string input;
    input.resize(2560);
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = (char)i;
    }
    testRoundTripContext(input);
}

TEST(FSEContextTest, testUncompressibleRoundTrip)
{
    std::string input;
    input.resize(10000);
    std::mt19937 gen(42);
    std::uniform_int_distribution<int8_t> dist;
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = dist(gen);
    }
    testRoundTripContext(input);
}

TEST(FSEContextTest, testUniformCompressibleRoundTrip)
{
    std::string input;
    input.resize(10000);
    std::mt19937 gen(42);
    std::uniform_int_distribution<int8_t> dist(0, 15);
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = dist(gen);
    }
    testRoundTripContext(input);
}

} // namespace
} // namespace tests
} // namespace openzl
