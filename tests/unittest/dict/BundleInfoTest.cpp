// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "openzl/dict/bundle.h"
#include "openzl/fse/common/mem.h"

#include "DictTestHelpers.h"

// ===========================================================================
// BundleInfo_parse tests
// ===========================================================================

// --- Failure modes ---

TEST(BundleInfoTest, PackDstTooSmall)
{
    auto buf         = buildPackedBundleInfo(2);
    auto parseResult = ZL_BundleInfo_parse(buf.data(), buf.size());
    ASSERT_FALSE(ZL_RES_isError(parseResult));
    ZL_BundleInfo info = ZL_RES_value(parseResult);

    size_t neededSize = ZL_BUNDLE_HEADER_SIZE + 2 * ZL_UNIQUE_ID_SIZE;
    std::vector<uint8_t> dst(neededSize - 1);

    ZL_Report r = BundleInfo_pack(dst.data(), dst.size(), &info);
    ASSERT_TRUE(ZL_isError(r));
    EXPECT_EQ(ZL_errorCode(r), ZL_ErrorCode_dstCapacity_tooSmall);
}

TEST(BundleInfoTest, ParseNullBuffer)
{
    auto result = ZL_BundleInfo_parse(nullptr, 100);
    ASSERT_TRUE(ZL_RES_isError(result));
    EXPECT_EQ(ZL_RES_code(result), ZL_ErrorCode_dict_corruption);
}

TEST(BundleInfoTest, ParseBufferOneByteTooSmall)
{
    std::vector<uint8_t> buf(ZL_BUNDLE_HEADER_SIZE - 1, 0);
    MEM_writeLE32(buf.data(), ZL_BUNDLEINFO_MAGIC);

    auto result = ZL_BundleInfo_parse(buf.data(), buf.size());
    ASSERT_TRUE(ZL_RES_isError(result));
    EXPECT_EQ(ZL_RES_code(result), ZL_ErrorCode_dict_corruption);
}

TEST(BundleInfoTest, ParseWrongMagic)
{
    auto buf = buildPackedBundleInfo(0);
    MEM_writeLE32(buf.data(), 0xBADCAFE);

    auto result = ZL_BundleInfo_parse(buf.data(), buf.size());
    ASSERT_TRUE(ZL_RES_isError(result));
    EXPECT_EQ(ZL_RES_code(result), ZL_ErrorCode_dict_corruption);
}

TEST(BundleInfoTest, ParseDictArrayTruncatedByOneByte)
{
    auto buf = buildPackedBundleInfo(3);
    buf.resize(buf.size() - 1);

    auto result = ZL_BundleInfo_parse(buf.data(), buf.size());
    ASSERT_TRUE(ZL_RES_isError(result));
    EXPECT_EQ(ZL_RES_code(result), ZL_ErrorCode_dict_corruption);
}

// --- Success modes ---

TEST(BundleInfoTest, ParseOversizedBufferSucceeds)
{
    auto buf = buildPackedBundleInfo(2);
    buf.resize(buf.size() + 200, 0xFF);

    auto result = ZL_BundleInfo_parse(buf.data(), buf.size());
    ASSERT_FALSE(ZL_RES_isError(result));
    ZL_BundleInfo info = ZL_RES_value(result);
    EXPECT_EQ(info.numDicts, 2u);
    EXPECT_EQ(info.packedSize, ZL_BUNDLE_HEADER_SIZE + 2 * ZL_UNIQUE_ID_SIZE);
}

TEST(BundleInfoTest, PackPackParseRoundTripZeroDicts)
{
    auto buf         = buildPackedBundleInfo(0, 10);
    auto parseResult = ZL_BundleInfo_parse(buf.data(), buf.size());
    ASSERT_FALSE(ZL_RES_isError(parseResult));
    ZL_BundleInfo info = ZL_RES_value(parseResult);
    ASSERT_EQ(info.packedSize, ZL_BUNDLE_HEADER_SIZE);

    std::vector<uint8_t> repacked(ZL_BUNDLE_HEADER_SIZE);
    ZL_Report r = BundleInfo_pack(repacked.data(), repacked.size(), &info);
    ASSERT_FALSE(ZL_isError(r));
    EXPECT_EQ(ZL_validResult(r), ZL_BUNDLE_HEADER_SIZE);

    auto parseResult2 = ZL_BundleInfo_parse(repacked.data(), ZL_validResult(r));
    ASSERT_FALSE(ZL_RES_isError(parseResult2));
    ZL_BundleInfo info2 = ZL_RES_value(parseResult2);
    EXPECT_EQ(info2.numDicts, 0u);
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(info2.bundleID.id.bytes[i], info.bundleID.id.bytes[i]);
    }
}

TEST(BundleInfoTest, PackPackParseRoundTripMultipleDicts)
{
    constexpr size_t numDicts = 4;
    size_t packedSize = ZL_BUNDLE_HEADER_SIZE + numDicts * ZL_UNIQUE_ID_SIZE;

    auto buf         = buildPackedBundleInfo(numDicts, 33);
    auto parseResult = ZL_BundleInfo_parse(buf.data(), buf.size());
    ASSERT_FALSE(ZL_RES_isError(parseResult));
    ZL_BundleInfo info = ZL_RES_value(parseResult);
    ASSERT_EQ(info.packedSize, packedSize);

    std::vector<uint8_t> repacked(packedSize);
    ZL_Report r = BundleInfo_pack(repacked.data(), repacked.size(), &info);
    ASSERT_FALSE(ZL_isError(r));
    EXPECT_EQ(ZL_validResult(r), packedSize);

    EXPECT_EQ(std::memcmp(repacked.data(), buf.data(), packedSize), 0);
}
