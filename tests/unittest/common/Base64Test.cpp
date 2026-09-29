// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "openzl/shared/base64.h"

namespace openzl::tests {

// RFC 4648 §10 test vectors
struct RFC4648Vector {
    std::string_view plain;
    std::string_view encoded;
};

static const RFC4648Vector kRFC4648Vectors[] = {
    { "", "" },
    { "f", "Zg==" },
    { "fo", "Zm8=" },
    { "foo", "Zm9v" },
    { "foob", "Zm9vYg==" },
    { "fooba", "Zm9vYmE=" },
    { "foobar", "Zm9vYmFy" },
};

TEST(Base64Test, RFC4648RoundTrip)
{
    for (const auto& vec : kRFC4648Vectors) {
        // Encode
        const size_t encSize = ZL_base64EncodedSize(vec.plain.size());
        ASSERT_EQ(encSize, vec.encoded.size())
                << "EncodedSize mismatch for \"" << vec.plain << "\"";

        std::vector<char> encBuf(encSize + 1);
        const ZL_Report encReport = ZL_base64Encode(
                encBuf.data(),
                encBuf.size(),
                reinterpret_cast<const uint8_t*>(vec.plain.data()),
                vec.plain.size());
        ASSERT_FALSE(ZL_isError(encReport))
                << "Encode failed for \"" << vec.plain << "\"";
        const size_t written = ZL_validResult(encReport);
        ASSERT_EQ(written, vec.encoded.size())
                << "Encode length mismatch for \"" << vec.plain << "\"";
        EXPECT_EQ(std::string_view(encBuf.data(), written), vec.encoded)
                << "Encode output mismatch for \"" << vec.plain << "\"";

        // Decode
        std::vector<uint8_t> decBuf(vec.plain.size() + 1);
        const ZL_Report decReport = ZL_base64Decode(
                decBuf.data(),
                decBuf.size(),
                vec.encoded.data(),
                vec.encoded.size());
        ASSERT_FALSE(ZL_isError(decReport))
                << "Decode failed for \"" << vec.encoded << "\"";
        const size_t decoded = ZL_validResult(decReport);
        ASSERT_EQ(decoded, vec.plain.size())
                << "Decode length mismatch for \"" << vec.encoded << "\"";
        EXPECT_EQ(
                std::string_view(
                        reinterpret_cast<char*>(decBuf.data()), decoded),
                vec.plain)
                << "Decode output mismatch for \"" << vec.encoded << "\"";
    }
}

TEST(Base64Test, EncodedSize)
{
    EXPECT_EQ(ZL_base64EncodedSize(0), 0);
    EXPECT_EQ(ZL_base64EncodedSize(1), 4);
    EXPECT_EQ(ZL_base64EncodedSize(2), 4);
    EXPECT_EQ(ZL_base64EncodedSize(3), 4);
    EXPECT_EQ(ZL_base64EncodedSize(4), 8);
    EXPECT_EQ(ZL_base64EncodedSize(6), 8);
    EXPECT_EQ(ZL_base64EncodedSize(7), 12);
}

TEST(Base64Test, EncodeRejectsInsufficientCapacity)
{
    const uint8_t src[]  = "foo";
    const size_t srcSize = 3;
    const size_t needed  = ZL_base64EncodedSize(srcSize);

    // One byte too small
    std::vector<char> buf(needed);
    EXPECT_TRUE(
            ZL_isError(ZL_base64Encode(buf.data(), needed - 1, src, srcSize)));

    // Exact size succeeds
    ZL_Report report = ZL_base64Encode(buf.data(), needed, src, srcSize);
    ASSERT_FALSE(ZL_isError(report));
    EXPECT_EQ(ZL_validResult(report), needed);
}

TEST(Base64Test, DecodeRejectsInsufficientCapacity)
{
    const char* encoded     = "Zm9v"; // "foo"
    const size_t encodedLen = 4;

    // Decoded size is 3; capacity of 2 should fail
    uint8_t buf[3];
    EXPECT_TRUE(ZL_isError(ZL_base64Decode(buf, 2, encoded, encodedLen)));

    // Exact capacity succeeds
    ZL_Report report = ZL_base64Decode(buf, 3, encoded, encodedLen);
    ASSERT_FALSE(ZL_isError(report));
    EXPECT_EQ(ZL_validResult(report), 3);
}

TEST(Base64Test, DecodeRejectsInvalidLength)
{
    uint8_t buf[16];
    // Lengths that aren't multiples of 4 should return error
    EXPECT_TRUE(ZL_isError(ZL_base64Decode(buf, sizeof(buf), "Z", 1)));
    EXPECT_TRUE(ZL_isError(ZL_base64Decode(buf, sizeof(buf), "Zm", 2)));
    EXPECT_TRUE(ZL_isError(ZL_base64Decode(buf, sizeof(buf), "Zm9", 3)));
    // Multiple of 4 succeeds
    EXPECT_FALSE(ZL_isError(ZL_base64Decode(buf, sizeof(buf), "Zm9v", 4)));
}

TEST(Base64Test, DecodeRejectsMidStreamPadding)
{
    uint8_t buf[16];
    EXPECT_TRUE(ZL_isError(ZL_base64Decode(buf, sizeof(buf), "Zm==Zm9v", 8)));
    EXPECT_TRUE(ZL_isError(ZL_base64Decode(buf, sizeof(buf), "Zg==Zg==", 8)));
    EXPECT_TRUE(ZL_isError(ZL_base64Decode(buf, sizeof(buf), "Zm8=Zm8=", 8)));
}

TEST(Base64Test, DecodeRejectsInvalidChars)
{
    uint8_t buf[3];
    // Invalid characters should return an error
    EXPECT_TRUE(ZL_isError(ZL_base64Decode(buf, sizeof(buf), "!m9v", 4)));
    EXPECT_TRUE(ZL_isError(ZL_base64Decode(buf, sizeof(buf), "Zm\x019", 4)));
    EXPECT_TRUE(ZL_isError(ZL_base64Decode(buf, sizeof(buf), "Zm9@", 4)));

    // Valid input succeeds
    ZL_Report report = ZL_base64Decode(buf, sizeof(buf), "Zm9v", 4);
    ASSERT_FALSE(ZL_isError(report));
    EXPECT_EQ(ZL_validResult(report), 3);
}

TEST(Base64Test, EmptyInput)
{
    // Encoding empty input produces empty output (value 0, not error)
    char encBuf[1]      = { 'X' };
    ZL_Report encReport = ZL_base64Encode(encBuf, sizeof(encBuf), nullptr, 0);
    ASSERT_FALSE(ZL_isError(encReport));
    EXPECT_EQ(ZL_validResult(encReport), 0);

    // Decoding empty input returns success with value 0
    uint8_t decBuf[1]   = { 0xFF };
    ZL_Report decReport = ZL_base64Decode(decBuf, sizeof(decBuf), "", 0);
    ASSERT_FALSE(ZL_isError(decReport));
    EXPECT_EQ(ZL_validResult(decReport), 0);
}

TEST(Base64Test, BinaryRoundTrip)
{
    // Roundtrip all 256 byte values
    uint8_t src[256];
    for (int i = 0; i < 256; ++i) {
        src[i] = static_cast<uint8_t>(i);
    }

    const size_t encSize = ZL_base64EncodedSize(sizeof(src));
    std::vector<char> encBuf(encSize);
    ZL_Report encReport =
            ZL_base64Encode(encBuf.data(), encBuf.size(), src, sizeof(src));
    ASSERT_FALSE(ZL_isError(encReport));
    const size_t written = ZL_validResult(encReport);
    ASSERT_EQ(written, encSize);

    uint8_t decoded[256];
    ZL_Report decReport =
            ZL_base64Decode(decoded, sizeof(decoded), encBuf.data(), written);
    ASSERT_FALSE(ZL_isError(decReport));
    ASSERT_EQ(ZL_validResult(decReport), sizeof(src));
    EXPECT_EQ(std::memcmp(src, decoded, sizeof(src)), 0);
}

} // namespace openzl::tests
