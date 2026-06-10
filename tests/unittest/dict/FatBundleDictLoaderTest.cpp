// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include "openzl/zl_dictloader.h"
#include "openzl/zl_materializer.h"

#include "openzl/dict/dictloader.h" // ZL_RES_value(ZL_DictLoader_fetchDictBundl)e
#include "tests/unittest/dict/DictTestHelpers.h"

namespace {

// -- Mock materializer for testing --

static ZL_RESULT_OF(ZL_VoidPtr) mockMaterialize(
        ZL_Materializer* matCtx,
        const void* src,
        size_t srcSize) ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE(ZL_VoidPtr, nullptr);
    void* copy = ZL_Materializer_allocate(matCtx, srcSize);
    ZL_ERR_IF_NULL(copy, allocation);
    void* copy2 = malloc(srcSize);
    ZL_ERR_IF_NULL(copy2, allocation);
    void** ret = (void**)ZL_Materializer_allocate(matCtx, 2 * sizeof(void*));
    if (ret == nullptr) {
        free(copy2);
        ZL_ERR(allocation);
    }
    std::memcpy(copy, src, srcSize);
    std::memcpy(copy2, src, srcSize);
    ret[0] = copy;
    ret[1] = copy2;
    return ZL_WRAP_VALUE(ret);
}

static void mockDematerialize(ZL_Materializer* /*matCtx*/, void* src)
        ZL_NOEXCEPT_FUNC_PTR
{
    void** arr = (void**)src;
    free(arr[1]);
}

// Unconditionally fails
static ZL_RESULT_OF(ZL_VoidPtr) badMaterialize(
        ZL_Materializer* /*matCtx*/,
        const void* /*src*/,
        size_t /*srcSize*/) ZL_NOEXCEPT_FUNC_PTR
{
    ZL_RESULT_DECLARE_SCOPE(ZL_VoidPtr, nullptr);
    ZL_ERR(allocation);
}

static ZL_MaterializerDesc2 mockMat()
{
    ZL_MaterializerDesc2 mat = {};
    mat.materializeFn        = mockMaterialize;
    mat.dematerializeFn      = mockDematerialize;
    return mat;
}

static ZL_MaterializerDesc2 badMat()
{
    ZL_MaterializerDesc2 mat = {};
    mat.materializeFn        = badMaterialize;
    mat.dematerializeFn      = ZL_NOOP_DEMATERIALIZE;
    return mat;
}

// -- Helper to extract bundle ID from a packed fat bundle --

static ZL_BundleID extractBundleID(const std::vector<uint8_t>& fatBuf)
{
    ZL_BundleID bid;
    std::memset(&bid, 0, sizeof(bid));
    std::memcpy(&bid.id, fatBuf.data() + 4, sizeof(bid.id));
    return bid;
}

// Codec ID whose materializer always fails (for error-path tests)
static constexpr int kBadMatCodecID = 999;

class FatBundleDictLoaderTest : public ::testing::Test {
   protected:
    ZL_FatBundleDictLoader* loader = nullptr;
    ZL_DictLoader* baseLoader      = nullptr;

    void SetUp() override
    {
        loader = ZL_FatBundleDictLoader_create();
        ASSERT_NE(loader, nullptr);
        baseLoader = ZL_FatBundleDictLoader_getDictLoader(loader);

        // Register mock materializers for all codecs used by tests
        ZL_MaterializerDesc2 mock = mockMat();
        for (int id : { 0, 100, 200, 300 }) {
            ASSERT_FALSE(ZL_isError(
                    ZL_DictLoader_registerMaterializer(baseLoader, id, &mock)));
        }

        // Register a materializer that always fails
        ZL_MaterializerDesc2 bad = badMat();
        ASSERT_FALSE(ZL_isError(ZL_DictLoader_registerMaterializer(
                baseLoader, kBadMatCodecID, &bad)));
    }

    void TearDown() override
    {
        ZL_FatBundleDictLoader_free(loader);
    }
};

// ============================================================
// Tests
// ============================================================

TEST_F(FatBundleDictLoaderTest, FreeNullIsSafe)
{
    ZL_FatBundleDictLoader_free(nullptr); // should not crash
}

TEST_F(FatBundleDictLoaderTest, DoubleRegisterMaterializerIsError)
{
    // Codec 100 is already registered in SetUp; re-registering should fail
    ZL_MaterializerDesc2 mat = mockMat();
    EXPECT_TRUE(ZL_isError(
            ZL_DictLoader_registerMaterializer(baseLoader, 100, &mat)));
}

TEST_F(FatBundleDictLoaderTest, MissingMaterializerIsError)
{
    // No materializer registered for codec 99
    auto dict   = buildPackedDict(16, makeDictID(1), 99, 0xCC, true);
    auto fatBuf = packFatBundle({ dict });

    EXPECT_TRUE(ZL_isError(ZL_FatBundleDictLoader_loadFatBundle(
            loader, fatBuf.data(), fatBuf.size())));
}

TEST_F(FatBundleDictLoaderTest, MaterializeFailsIsError)
{
    auto dict = buildPackedDict(16, makeDictID(1), kBadMatCodecID, 0xDD, true);
    auto fatBuf = packFatBundle({ dict });

    EXPECT_TRUE(ZL_isError(ZL_FatBundleDictLoader_loadFatBundle(
            loader, fatBuf.data(), fatBuf.size())));
}

TEST_F(FatBundleDictLoaderTest, LoadNullBufferIsError)
{
    EXPECT_TRUE(ZL_isError(
            ZL_FatBundleDictLoader_loadFatBundle(loader, nullptr, 100)));
}

TEST_F(FatBundleDictLoaderTest, LoadBufferTooSmallIsError)
{
    std::vector<uint8_t> buf(ZL_BUNDLE_HEADER_SIZE - 1, 0);
    MEM_writeLE32(buf.data(), ZL_BUNDLEINFO_MAGIC);

    EXPECT_TRUE(ZL_isError(ZL_FatBundleDictLoader_loadFatBundle(
            loader, buf.data(), buf.size())));
}

TEST_F(FatBundleDictLoaderTest, LoadDictDataMissingIsError)
{
    auto dict   = buildPackedDict(20);
    auto fatBuf = packFatBundle({ dict });

    // Truncate to just the BundleInfo header (drop the dict data)
    size_t bundleInfoSize = ZL_BUNDLE_HEADER_SIZE + ZL_UNIQUE_ID_SIZE;
    fatBuf.resize(bundleInfoSize);

    EXPECT_TRUE(ZL_isError(ZL_FatBundleDictLoader_loadFatBundle(
            loader, fatBuf.data(), fatBuf.size())));
}

TEST_F(FatBundleDictLoaderTest, LoadDictDataPartiallyTruncatedIsError)
{
    auto dict   = buildPackedDict(100);
    auto fatBuf = packFatBundle({ dict });

    fatBuf.resize(fatBuf.size() - 1);

    EXPECT_TRUE(ZL_isError(ZL_FatBundleDictLoader_loadFatBundle(
            loader, fatBuf.data(), fatBuf.size())));
}

TEST_F(FatBundleDictLoaderTest, LoadSecondDictCorruptedIsError)
{
    auto dict1  = buildPackedDict(10, makeDictID(1), 0, 0xAB, true);
    auto dict2  = buildPackedDict(20, makeDictID(2), 0, 0xAB, true);
    auto fatBuf = packFatBundle({ dict1, dict2 });

    // Corrupt the magic bytes of the second dict
    size_t secondDictStart =
            ZL_BUNDLE_HEADER_SIZE + 2 * ZL_UNIQUE_ID_SIZE + dict1.size();
    MEM_writeLE32(fatBuf.data() + secondDictStart, 0xBADBAD00);

    EXPECT_TRUE(ZL_isError(ZL_FatBundleDictLoader_loadFatBundle(
            loader, fatBuf.data(), fatBuf.size())));
}

// --- Success modes ---

TEST_F(FatBundleDictLoaderTest, LoadEmptyBundle)
{
    auto fatBuf = packFatBundle({});
    ASSERT_FALSE(ZL_isError(ZL_FatBundleDictLoader_loadFatBundle(
            loader, fatBuf.data(), fatBuf.size())));

    ZL_BundleID bid = extractBundleID(fatBuf);
    const ZL_DictBundle* bundle =
            ZL_RES_value(ZL_DictLoader_fetchDictBundle(baseLoader, &bid));
    ASSERT_NE(bundle, nullptr);
    EXPECT_EQ(bundle->info.numDicts, 0u);
    EXPECT_EQ(bundle->dicts, nullptr);
}

TEST_F(FatBundleDictLoaderTest, LoadLargeContentDict)
{
    auto dict   = buildPackedDict(65536, makeDictID(1), 0, 0xAB, true);
    auto fatBuf = packFatBundle({ dict });

    ASSERT_FALSE(ZL_isError(ZL_FatBundleDictLoader_loadFatBundle(
            loader, fatBuf.data(), fatBuf.size())));

    ZL_BundleID bid = extractBundleID(fatBuf);
    const ZL_DictBundle* bundle =
            ZL_RES_value(ZL_DictLoader_fetchDictBundle(baseLoader, &bid));
    ASSERT_NE(bundle, nullptr);
    EXPECT_EQ(bundle->info.numDicts, 1u);
    ASSERT_NE(bundle->dicts, nullptr);
    ASSERT_NE(bundle->dicts[0], nullptr);
    EXPECT_EQ(bundle->dicts[0]->packedSize, ZL_DICT_HEADER_SIZE + 65536u);
    EXPECT_EQ(bundle->packedSize, fatBuf.size());
}

TEST_F(FatBundleDictLoaderTest, RoundTripMultipleDicts)
{
    auto dict1 = buildPackedDict(0, makeDictID(1), 100, 0xAA, true);
    auto dict2 = buildPackedDict(50, makeDictID(2), 200, 0xBB, true);
    auto dict3 = buildPackedDict(10, makeDictID(3), 300, 0xCC, true);

    auto fatBuf = packFatBundle({ dict1, dict2, dict3 });

    ASSERT_FALSE(ZL_isError(ZL_FatBundleDictLoader_loadFatBundle(
            loader, fatBuf.data(), fatBuf.size())));

    ZL_BundleID bid = extractBundleID(fatBuf);
    const ZL_DictBundle* bundle =
            ZL_RES_value(ZL_DictLoader_fetchDictBundle(baseLoader, &bid));
    ASSERT_NE(bundle, nullptr);
    EXPECT_EQ(bundle->info.numDicts, 3u);
    ASSERT_NE(bundle->dicts, nullptr);

    // Verify each dict was parsed with the correct materializing codec
    EXPECT_EQ(bundle->dicts[0]->materializingCodec, 100u);
    EXPECT_EQ(bundle->dicts[1]->materializingCodec, 200u);
    EXPECT_EQ(bundle->dicts[2]->materializingCodec, 300u);

    // Verify each dict was parsed with the correct codec type
    EXPECT_TRUE(bundle->dicts[0]->isCustomCodec);
    EXPECT_TRUE(bundle->dicts[1]->isCustomCodec);
    EXPECT_TRUE(bundle->dicts[2]->isCustomCodec);

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

TEST_F(FatBundleDictLoaderTest, DuplicateBundleIsIdempotent)
{
    auto dict   = buildPackedDict(16, makeDictID(1), 100, 0xDD, true);
    auto fatBuf = packFatBundle({ dict });
    auto bid    = extractBundleID(fatBuf);

    // Loading the same bundle twice should succeed (idempotent)
    EXPECT_FALSE(ZL_isError(ZL_FatBundleDictLoader_loadFatBundle(
            loader, fatBuf.data(), fatBuf.size())));
    const ZL_DictBundle* bundle1 =
            ZL_RES_value(ZL_DictLoader_fetchDictBundle(baseLoader, &bid));

    EXPECT_FALSE(ZL_isError(ZL_FatBundleDictLoader_loadFatBundle(
            loader, fatBuf.data(), fatBuf.size())));
    const ZL_DictBundle* bundle2 =
            ZL_RES_value(ZL_DictLoader_fetchDictBundle(baseLoader, &bid));

    // The two calls should point to the same bundle
    ASSERT_EQ(bundle1, bundle2);
}

TEST_F(FatBundleDictLoaderTest, DictDedupAcrossBundles)
{
    auto sharedDict  = buildPackedDict(10, makeDictID(1), 100, 0xAA, true);
    auto uniqueDict1 = buildPackedDict(20, makeDictID(2), 200, 0xBB, true);
    auto uniqueDict2 = buildPackedDict(30, makeDictID(3), 200, 0xCC, true);

    auto fatBuf1 = packFatBundle({ sharedDict, uniqueDict1 });
    auto fatBuf2 = packFatBundle({ sharedDict, uniqueDict2 });

    ASSERT_FALSE(ZL_isError(ZL_FatBundleDictLoader_loadFatBundle(
            loader, fatBuf1.data(), fatBuf1.size())));
    ASSERT_FALSE(ZL_isError(ZL_FatBundleDictLoader_loadFatBundle(
            loader, fatBuf2.data(), fatBuf2.size())));

    // Fetch both bundles and verify the shared dict pointer is identical
    ZL_BundleID bid1 = extractBundleID(fatBuf1);
    ZL_BundleID bid2 = extractBundleID(fatBuf2);

    const ZL_DictBundle* bundle1 =
            ZL_RES_value(ZL_DictLoader_fetchDictBundle(baseLoader, &bid1));
    const ZL_DictBundle* bundle2 =
            ZL_RES_value(ZL_DictLoader_fetchDictBundle(baseLoader, &bid2));
    ASSERT_NE(bundle1, nullptr);
    ASSERT_NE(bundle2, nullptr);
    ASSERT_EQ(bundle1->info.numDicts, 2u);
    ASSERT_EQ(bundle2->info.numDicts, 2u);

    // The shared dict (index 0 in both bundles) should be the same pointer
    EXPECT_EQ(bundle1->dicts[0], bundle2->dicts[0]);

    // The unique dicts should be different
    EXPECT_NE(bundle1->dicts[1], bundle2->dicts[1]);
}

TEST_F(FatBundleDictLoaderTest, DictDedupConflictIsError)
{
    // Two bundles share the same dictID but with different content bytes,
    // producing different content hashes. The second load must fail with
    // dict_corruption.
    auto sharedID = makeDictID(42);

    auto sharedDict1 = buildPackedDict(16, sharedID, 100, 0xAA, true);
    auto otherDict1  = buildPackedDict(20, makeDictID(2), 200, 0xBB, true);
    auto fatBuf1     = packFatBundle({ sharedDict1, otherDict1 });

    auto sharedDict2 = buildPackedDict(16, sharedID, 100, 0xBB, true);
    auto otherDict2  = buildPackedDict(30, makeDictID(3), 200, 0xCC, true);
    auto fatBuf2     = packFatBundle({ sharedDict2, otherDict2 });

    ASSERT_FALSE(ZL_isError(ZL_FatBundleDictLoader_loadFatBundle(
            loader, fatBuf1.data(), fatBuf1.size())));

    // Second load should fail because the same dictID has different content
    EXPECT_TRUE(ZL_isError(ZL_FatBundleDictLoader_loadFatBundle(
            loader, fatBuf2.data(), fatBuf2.size())));
}

} // namespace
