// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <vector>

#include "openzl/common/allocation.h"
#include "openzl/dict/bundle.h"
#include "openzl/dict/dict.h"
#include "openzl/fse/common/mem.h"

#include "DictTestHelpers.h"

class FatBundleTest : public ::testing::Test {
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

TEST_F(FatBundleTest, PackDstTooSmall)
{
    auto dict = buildPackedDict(10);

    const void* dicts[] = { dict.data() };
    size_t dictSizes[]  = { dict.size() };
    size_t needed = ZL_BUNDLE_HEADER_SIZE + ZL_UNIQUE_ID_SIZE + dict.size();

    std::vector<uint8_t> dst(needed - 1);
    ZL_Report r = ZL_DictBundle_packFatBundle(
            dst.data(), dst.size(), dicts, dictSizes, 1);
    ASSERT_TRUE(ZL_isError(r));
    EXPECT_EQ(ZL_errorCode(r), ZL_ErrorCode_dstCapacity_tooSmall);
}

TEST_F(FatBundleTest, ParseNullBuffer)
{
    auto r = ZL_DictBundle_parseFatBundle(nullptr, 100, arena_);
    ASSERT_TRUE(ZL_RES_isError(r));
}

TEST_F(FatBundleTest, ParseBufferTooSmall)
{
    std::vector<uint8_t> buf(ZL_BUNDLE_HEADER_SIZE - 1, 0);
    MEM_writeLE32(buf.data(), ZL_BUNDLEINFO_MAGIC);

    auto r = ZL_DictBundle_parseFatBundle(buf.data(), buf.size(), arena_);
    ASSERT_TRUE(ZL_RES_isError(r));
}

TEST_F(FatBundleTest, ParseDictDataMissing)
{
    auto dict   = buildPackedDict(20);
    auto fatBuf = packFatBundle({ dict });

    size_t bundleInfoSize = ZL_BUNDLE_HEADER_SIZE + ZL_UNIQUE_ID_SIZE;
    fatBuf.resize(bundleInfoSize);

    auto r = ZL_DictBundle_parseFatBundle(fatBuf.data(), fatBuf.size(), arena_);
    ASSERT_TRUE(ZL_RES_isError(r));
}

TEST_F(FatBundleTest, ParseDictDataPartiallyTruncated)
{
    auto dict   = buildPackedDict(100);
    auto fatBuf = packFatBundle({ dict });

    fatBuf.resize(fatBuf.size() - 1);

    auto r = ZL_DictBundle_parseFatBundle(fatBuf.data(), fatBuf.size(), arena_);
    ASSERT_TRUE(ZL_RES_isError(r));
}

TEST_F(FatBundleTest, ParseSecondDictCorrupted)
{
    auto dict1  = buildPackedDict(10);
    auto dict2  = buildPackedDict(20);
    auto fatBuf = packFatBundle({ dict1, dict2 });

    size_t secondDictStart =
            ZL_BUNDLE_HEADER_SIZE + 2 * ZL_UNIQUE_ID_SIZE + dict1.size();
    MEM_writeLE32(fatBuf.data() + secondDictStart, 0xBADBAD00);

    auto r = ZL_DictBundle_parseFatBundle(fatBuf.data(), fatBuf.size(), arena_);
    ASSERT_TRUE(ZL_RES_isError(r));
}

// --- Success modes ---

TEST_F(FatBundleTest, ParseEmptyBundle)
{
    auto fatBuf = packFatBundle({});

    auto r = ZL_DictBundle_parseFatBundle(fatBuf.data(), fatBuf.size(), arena_);
    ASSERT_FALSE(ZL_RES_isError(r));
    const ZL_DictBundle* bundle = ZL_RES_value(r);
    ASSERT_NE(bundle, nullptr);
    EXPECT_EQ(bundle->info.numDicts, 0u);
    EXPECT_EQ(bundle->dicts, nullptr);
}

TEST_F(FatBundleTest, ParseLargeContentDict)
{
    auto dict   = buildPackedDict(65536);
    auto fatBuf = packFatBundle({ dict });

    auto r = ZL_DictBundle_parseFatBundle(fatBuf.data(), fatBuf.size(), arena_);
    ASSERT_FALSE(ZL_RES_isError(r));
    const ZL_DictBundle* bundle = ZL_RES_value(r);
    ASSERT_NE(bundle, nullptr);
    EXPECT_EQ(bundle->info.numDicts, 1u);
    ASSERT_NE(bundle->dicts, nullptr);
    ASSERT_NE(bundle->dicts[0], nullptr);
    EXPECT_EQ(bundle->dicts[0]->packedSize, ZL_DICT_HEADER_SIZE + 65536u);
    EXPECT_EQ(bundle->packedSize, fatBuf.size());
}

TEST_F(FatBundleTest, RoundTripMultipleDicts)
{
    auto dict1 = buildPackedDict(0, makeDictID(1), 100, 0xAA, trt_standard);
    auto dict2 = buildPackedDict(50, makeDictID(2), 200, 0xBB, trt_custom);
    auto dict3 = buildPackedDict(10, makeDictID(3), 300, 0xCC, trt_standard);

    auto fatBuf = packFatBundle({ dict1, dict2, dict3 });

    auto r = ZL_DictBundle_parseFatBundle(fatBuf.data(), fatBuf.size(), arena_);
    ASSERT_FALSE(ZL_RES_isError(r));

    const ZL_DictBundle* bundle = ZL_RES_value(r);
    ASSERT_NE(bundle, nullptr);
    EXPECT_EQ(bundle->info.numDicts, 3u);
    ASSERT_NE(bundle->dicts, nullptr);

    // Verify each dict was parsed with the correct materializing codec
    EXPECT_EQ(bundle->dicts[0]->materializingCodec, 100u);
    EXPECT_EQ(bundle->dicts[1]->materializingCodec, 200u);
    EXPECT_EQ(bundle->dicts[2]->materializingCodec, 300u);

    // Verify each dict was parsed with the correct codec type
    EXPECT_EQ(bundle->dicts[0]->codecType, trt_standard);
    EXPECT_EQ(bundle->dicts[1]->codecType, trt_custom);
    EXPECT_EQ(bundle->dicts[2]->codecType, trt_standard);

    // Verify per-dict packedSize
    EXPECT_EQ(bundle->dicts[0]->packedSize, ZL_DICT_HEADER_SIZE + 0u);
    EXPECT_EQ(bundle->dicts[1]->packedSize, ZL_DICT_HEADER_SIZE + 50u);
    EXPECT_EQ(bundle->dicts[2]->packedSize, ZL_DICT_HEADER_SIZE + 10u);

    // Verify total bundle packedSize
    EXPECT_EQ(bundle->packedSize, fatBuf.size());

    // Verify dict IDs match what was packed
    for (size_t i = 0; i < 3; i++) {
        ASSERT_NE(bundle->dicts[i], nullptr) << "dict=" << i;
        EXPECT_EQ(
                memcmp(&bundle->dicts[i]->dictID,
                       &bundle->info.dictIDs[i],
                       sizeof(ZL_DictID)),
                0)
                << "dictID mismatch at index " << i;
    }
}
