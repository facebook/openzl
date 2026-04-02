// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <cstring>
#include <numeric>
#include <vector>

#include "openzl/common/unique_id.h"
#include "openzl/dict/dict.h"
#include "openzl/fse/common/mem.h"

#include "DictTestHelpers.h"

// --- Failure modes ---

TEST(DictTest, PackDstTooSmall)
{
    constexpr size_t contentSize = 10;
    std::vector<uint8_t> content(contentSize, 0xAA);
    std::vector<uint8_t> dst(ZL_DICT_HEADER_SIZE + contentSize - 1);

    ZL_Report r = Dict_pack(
            dst.data(),
            dst.size(),
            makeNullDictID(),
            0,
            trt_standard,
            content.data(),
            contentSize);
    EXPECT_EQ(ZL_errorCode(r), ZL_ErrorCode_dstCapacity_tooSmall);
}

TEST(DictTest, ParseNullBuffer)
{
    auto result = Dict_parse(nullptr, 100);
    ASSERT_TRUE(ZL_RES_isError(result));
    EXPECT_EQ(ZL_RES_code(result), ZL_ErrorCode_dict_corruption);
}

TEST(DictTest, ParseBufferOneByteTooSmall)
{
    std::vector<uint8_t> buf(ZL_DICT_HEADER_SIZE - 1, 0);
    MEM_writeLE32(buf.data(), ZL_DICT_MAGIC);

    auto result = Dict_parse(buf.data(), buf.size());
    ASSERT_TRUE(ZL_RES_isError(result));
    EXPECT_EQ(ZL_RES_code(result), ZL_ErrorCode_dict_corruption);
}

TEST(DictTest, ParseWrongMagic)
{
    auto buf = buildPackedDict(0);
    MEM_writeLE32(buf.data(), 0xDEADBEEF);

    auto result = Dict_parse(buf.data(), buf.size());
    ASSERT_TRUE(ZL_RES_isError(result));
    EXPECT_EQ(ZL_RES_code(result), ZL_ErrorCode_dict_corruption);
}

TEST(DictTest, ParseContentTruncatedByOneByte)
{
    auto buf = buildPackedDict(10);
    buf.resize(buf.size() - 1);

    auto result = Dict_parse(buf.data(), buf.size());
    ASSERT_TRUE(ZL_RES_isError(result));
    EXPECT_EQ(ZL_RES_code(result), ZL_ErrorCode_dict_corruption);
}

// --- Success modes ---

TEST(DictTest, PackEmptyContent)
{
    std::vector<uint8_t> dst(ZL_DICT_HEADER_SIZE);
    uint8_t dummy = 0;
    ZL_Report r   = Dict_pack(
            dst.data(),
            dst.size(),
            makeNullDictID(),
            0,
            trt_standard,
            &dummy,
            0);
    ASSERT_FALSE(ZL_isError(r));
    EXPECT_EQ(ZL_validResult(r), ZL_DICT_HEADER_SIZE);
}

TEST(DictTest, PackExactSizeBuffer)
{
    constexpr size_t contentSize = 20;
    std::vector<uint8_t> content(contentSize, 0xBB);
    std::vector<uint8_t> dst(ZL_DICT_HEADER_SIZE + contentSize);

    ZL_Report r = Dict_pack(
            dst.data(),
            dst.size(),
            makeNullDictID(),
            0,
            trt_standard,
            content.data(),
            contentSize);
    ASSERT_FALSE(ZL_isError(r));
    EXPECT_EQ(ZL_validResult(r), ZL_DICT_HEADER_SIZE + contentSize);
}

TEST(DictTest, PackNullIDAutoGeneratesFromContent)
{
    constexpr size_t contentSize = 16;
    std::vector<uint8_t> content(contentSize);
    std::iota(content.begin(), content.end(), static_cast<uint8_t>(1));
    std::vector<uint8_t> dst(ZL_DICT_HEADER_SIZE + contentSize);

    ZL_Report r = Dict_pack(
            dst.data(),
            dst.size(),
            makeNullDictID(),
            0,
            trt_standard,
            content.data(),
            contentSize);
    ASSERT_FALSE(ZL_isError(r));

    auto parseResult = Dict_parse(dst.data(), ZL_validResult(r));
    ASSERT_FALSE(ZL_RES_isError(parseResult));
    auto parsed = ZL_RES_value(parseResult);

    auto contentSha = ZL_UniqueID_computeSHA256(content.data(), contentSize);
    EXPECT_EQ(
            std::memcmp(&parsed.dictID.id, &contentSha, sizeof(contentSha)), 0);
}

TEST(DictTest, ParseEmptyContent)
{
    auto buf    = buildPackedDict(0);
    auto result = Dict_parse(buf.data(), buf.size());
    ASSERT_FALSE(ZL_RES_isError(result));
    auto parsed = ZL_RES_value(result);

    EXPECT_EQ(parsed.contentSize, 0u);
    EXPECT_EQ(parsed.packedSize, ZL_DICT_HEADER_SIZE);
    EXPECT_EQ(parsed.materializingCodec, 0u);
}

TEST(DictTest, ParsePackedSizeEqualsHeaderPlusContent)
{
    for (size_t sz : { 1, 7, 64, 255, 1024, 4096 }) {
        auto buf    = buildPackedDict(sz);
        auto result = Dict_parse(buf.data(), buf.size());
        ASSERT_FALSE(ZL_RES_isError(result)) << "contentSize=" << sz;
        EXPECT_EQ(ZL_RES_value(result).packedSize, ZL_DICT_HEADER_SIZE + sz)
                << "contentSize=" << sz;
    }
}

TEST(DictTest, ParseOversizedBufferSucceeds)
{
    constexpr size_t contentSize = 10;
    auto buf                     = buildPackedDict(contentSize);
    buf.resize(buf.size() + 100, 0xFF);

    auto result = Dict_parse(buf.data(), buf.size());
    ASSERT_FALSE(ZL_RES_isError(result));
    auto parsed = ZL_RES_value(result);
    EXPECT_EQ(parsed.contentSize, contentSize);
    EXPECT_EQ(parsed.packedSize, ZL_DICT_HEADER_SIZE + contentSize);
}

TEST(DictTest, RoundtripAllFieldsPreserved)
{
    ZL_DictID id                 = makeDictID(55);
    ZL_IDType codec              = 7777;
    constexpr size_t contentSize = 64;
    std::vector<uint8_t> content(contentSize);
    std::iota(content.begin(), content.end(), static_cast<uint8_t>(0));
    std::vector<uint8_t> dst(ZL_DICT_HEADER_SIZE + contentSize);

    ZL_Report r = Dict_pack(
            dst.data(),
            dst.size(),
            id,
            codec,
            trt_custom,
            content.data(),
            contentSize);
    ASSERT_FALSE(ZL_isError(r));
    size_t packedBytes = ZL_validResult(r);
    EXPECT_EQ(packedBytes, ZL_DICT_HEADER_SIZE + contentSize);

    auto parseResult = Dict_parse(dst.data(), packedBytes);
    ASSERT_FALSE(ZL_RES_isError(parseResult));
    auto parsed = ZL_RES_value(parseResult);

    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(parsed.dictID.id.bytes[i], id.id.bytes[i]) << "i=" << i;
    }
    EXPECT_EQ(parsed.materializingCodec, codec);
    EXPECT_EQ(parsed.codecType, trt_custom);
    EXPECT_EQ(parsed.contentSize, contentSize);
    EXPECT_EQ(parsed.packedSize, packedBytes);
    EXPECT_EQ(std::memcmp(parsed.dictContent, content.data(), contentSize), 0);
}
