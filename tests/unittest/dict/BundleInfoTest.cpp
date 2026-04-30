// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "openzl/common/allocation.h"
#include "openzl/dict/bundle.h"
#include "openzl/fse/common/mem.h"

#include "DictTestHelpers.h"

// ===========================================================================
// BundleInfo_parse tests
// ===========================================================================

class BundleInfoTest : public ::testing::Test {
   protected:
    void SetUp() override
    {
        arena_ = ALLOC_HeapArena_create();
    }
    void TearDown() override
    {
        ALLOC_Arena_freeArena(arena_);
    }
    Arena* arena_ = nullptr;
};

// --- Failure modes ---

TEST_F(BundleInfoTest, PackDstTooSmall)
{
    auto buf = buildPackedBundleInfo(2);
    ZL_BundleInfo info;
    auto parseResult = BundleInfo_parse(&info, buf.data(), buf.size(), arena_);
    ASSERT_FALSE(ZL_RES_isError(parseResult));

    size_t neededSize = ZL_BUNDLE_HEADER_SIZE + 2 * ZL_UNIQUE_ID_SIZE;
    std::vector<uint8_t> dst(neededSize - 1);

    ZL_Report r = BundleInfo_pack(dst.data(), dst.size(), &info);
    ASSERT_TRUE(ZL_isError(r));
    EXPECT_EQ(ZL_errorCode(r), ZL_ErrorCode_dstCapacity_tooSmall);
}

TEST_F(BundleInfoTest, ParseNullBuffer)
{
    ZL_BundleInfo info;
    auto result = BundleInfo_parse(&info, nullptr, 100, arena_);
    ASSERT_TRUE(ZL_RES_isError(result));
    EXPECT_EQ(ZL_RES_code(result), ZL_ErrorCode_dict_corruption);
}

TEST_F(BundleInfoTest, ParseBufferOneByteTooSmall)
{
    std::vector<uint8_t> buf(ZL_BUNDLE_HEADER_SIZE - 1, 0);
    MEM_writeLE32(buf.data(), ZL_BUNDLEINFO_MAGIC);

    ZL_BundleInfo info;
    auto result = BundleInfo_parse(&info, buf.data(), buf.size(), arena_);
    ASSERT_TRUE(ZL_RES_isError(result));
    EXPECT_EQ(ZL_RES_code(result), ZL_ErrorCode_dict_corruption);
}

TEST_F(BundleInfoTest, ParseWrongMagic)
{
    auto buf = buildPackedBundleInfo(0);
    MEM_writeLE32(buf.data(), 0xBADCAFE);

    ZL_BundleInfo info;
    auto result = BundleInfo_parse(&info, buf.data(), buf.size(), arena_);
    ASSERT_TRUE(ZL_RES_isError(result));
    EXPECT_EQ(ZL_RES_code(result), ZL_ErrorCode_dict_corruption);
}

TEST_F(BundleInfoTest, ParseDictArrayTruncatedByOneByte)
{
    auto buf = buildPackedBundleInfo(3);
    buf.resize(buf.size() - 1);

    ZL_BundleInfo info;
    auto result = BundleInfo_parse(&info, buf.data(), buf.size(), arena_);
    ASSERT_TRUE(ZL_RES_isError(result));
    EXPECT_EQ(ZL_RES_code(result), ZL_ErrorCode_dict_corruption);
}

// --- Success modes ---

TEST_F(BundleInfoTest, ParseOversizedBufferSucceeds)
{
    auto buf = buildPackedBundleInfo(2);
    buf.resize(buf.size() + 200, 0xFF);

    ZL_BundleInfo info;
    auto result = BundleInfo_parse(&info, buf.data(), buf.size(), arena_);
    ASSERT_FALSE(ZL_RES_isError(result));
    EXPECT_EQ(info.numDicts, 2u);
    EXPECT_EQ(
            ZL_RES_value(result),
            ZL_BUNDLE_HEADER_SIZE + 2 * ZL_UNIQUE_ID_SIZE);
}

TEST_F(BundleInfoTest, PackPackParseRoundTripZeroDicts)
{
    auto buf = buildPackedBundleInfo(0, 10);
    ZL_BundleInfo info;
    auto parseResult = BundleInfo_parse(&info, buf.data(), buf.size(), arena_);
    ASSERT_FALSE(ZL_RES_isError(parseResult));
    ASSERT_EQ(ZL_RES_value(parseResult), ZL_BUNDLE_HEADER_SIZE);

    std::vector<uint8_t> repacked(ZL_BUNDLE_HEADER_SIZE);
    ZL_Report r = BundleInfo_pack(repacked.data(), repacked.size(), &info);
    ASSERT_FALSE(ZL_isError(r));
    EXPECT_EQ(ZL_validResult(r), ZL_BUNDLE_HEADER_SIZE);

    Arena* arena2 = ALLOC_HeapArena_create();
    ZL_BundleInfo info2;
    auto parseResult2 = BundleInfo_parse(
            &info2, repacked.data(), ZL_validResult(r), arena2);
    ASSERT_FALSE(ZL_RES_isError(parseResult2));
    EXPECT_EQ(info2.numDicts, 0u);
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(info2.bundleID.id.bytes[i], info.bundleID.id.bytes[i]);
    }
    ALLOC_Arena_freeArena(arena2);
}

TEST_F(BundleInfoTest, PackPackParseRoundTripMultipleDicts)
{
    constexpr size_t numDicts = 4;
    size_t packedSize = ZL_BUNDLE_HEADER_SIZE + numDicts * ZL_UNIQUE_ID_SIZE;

    auto buf = buildPackedBundleInfo(numDicts, 33);
    ZL_BundleInfo info;
    auto parseResult = BundleInfo_parse(&info, buf.data(), buf.size(), arena_);
    ASSERT_FALSE(ZL_RES_isError(parseResult));
    ASSERT_EQ(ZL_RES_value(parseResult), packedSize);

    std::vector<uint8_t> repacked(packedSize);
    ZL_Report r = BundleInfo_pack(repacked.data(), repacked.size(), &info);
    ASSERT_FALSE(ZL_isError(r));
    EXPECT_EQ(ZL_validResult(r), packedSize);

    EXPECT_EQ(std::memcmp(repacked.data(), buf.data(), packedSize), 0);
}
