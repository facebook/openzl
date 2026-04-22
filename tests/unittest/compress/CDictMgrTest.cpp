// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "openzl/common/unique_id.h"
#include "openzl/compress/cdictmgr.h"
#include "openzl/dict/bundle.h"
#include "openzl/dict/dict.h"

#include "tests/unittest/compress/CDictMgrTestHelpers.h"
#include "tests/unittest/dict/DictTestHelpers.h"

class CDictMgrTest : public ::testing::Test {
   protected:
    void SetUp() override
    {
        mockNodes_.addDictNode(makeDictID(1));
        mockNodes_.addDictNode(makeDictID(2));
        mockNodes_.addDictNode(makeDictID(3));
        mockNodes_.addDictNode(makeNullDictID());
        mockNodes_.addDictNode(makeNullDictID());
        mockNodes_.addDictNode(makeDictID(5));
        mockNodes_.addDictNode(makeNullDictID());
        mockNodes_.addDictNode(makeDictID(7));

        ZL_Report r = CDictMgr_init(
                &mgr_, mockNodes_.nodesManager(), NULL, mockNodes_.opCtx());
        ASSERT_FALSE(ZL_isError(r));
    }
    void TearDown() override
    {
        CDictMgr_destroy(&mgr_);
    }

    MockNodesMgr mockNodes_;
    CDictMgr mgr_{};
};

// --- LoadFatBundle ---

TEST_F(CDictMgrTest, LoadFatBundleSingleDict)
{
    auto dict   = buildPackedDict(32, makeDictID(1), 100, 0xAA, true);
    auto fatBuf = packFatBundle({ dict });

    auto r = CDictMgr_loadFatBundle(&mgr_, fatBuf.data(), fatBuf.size());
    ASSERT_FALSE(ZL_RES_isError(r));
    const ZL_DictBundle* bundle = ZL_RES_value(r);
    EXPECT_EQ(bundle->info.numDicts, 1u);
    EXPECT_EQ(bundle->dicts[0]->materializingCodec, 100u);

    ZL_DictID id             = makeDictID(1);
    ZL_MaterializerDesc2 mat = makeDefaultDictMaterializer();
    const ZL_Dict* found     = CDictMgr_findDict(&mgr_, &id, &mat);
    ASSERT_EQ(found, bundle->dicts[0]);
}

TEST_F(CDictMgrTest, LoadFatBundleMultipleDicts)
{
    auto dict1 = buildPackedDict(10, makeDictID(1), 100, 0xAA, true);
    auto dict2 = buildPackedDict(20, makeDictID(2), 200, 0xBB, true);
    auto dict3 = buildPackedDict(30, makeDictID(3), 300, 0xCC, true);

    auto fatBuf = packFatBundle({ dict1, dict2, dict3 });

    auto r = CDictMgr_loadFatBundle(&mgr_, fatBuf.data(), fatBuf.size());
    ASSERT_FALSE(ZL_RES_isError(r));

    const ZL_DictBundle* bundle = ZL_RES_value(r);
    EXPECT_EQ(bundle->info.numDicts, 3u);
    EXPECT_EQ(bundle->dicts[0]->materializingCodec, 100u);
    EXPECT_EQ(bundle->dicts[1]->materializingCodec, 200u);
    EXPECT_EQ(bundle->dicts[2]->materializingCodec, 300u);

    ZL_DictID id1            = makeDictID(1);
    ZL_MaterializerDesc2 mat = makeDefaultDictMaterializer();
    const ZL_Dict* found1    = CDictMgr_findDict(&mgr_, &id1, &mat);
    ASSERT_EQ(found1, bundle->dicts[0]);

    ZL_DictID id2         = makeDictID(2);
    const ZL_Dict* found2 = CDictMgr_findDict(&mgr_, &id2, &mat);
    ASSERT_EQ(found2, bundle->dicts[1]);

    ZL_DictID id3         = makeDictID(3);
    const ZL_Dict* found3 = CDictMgr_findDict(&mgr_, &id3, &mat);
    ASSERT_EQ(found3, bundle->dicts[2]);
}

TEST_F(CDictMgrTest, LoadEmptyBundle)
{
    auto fatBuf = packFatBundle({});

    auto r = CDictMgr_loadFatBundle(&mgr_, fatBuf.data(), fatBuf.size());
    ASSERT_FALSE(ZL_RES_isError(r));

    const ZL_DictBundle* bundle = ZL_RES_value(r);
    ASSERT_NE(bundle, nullptr);
    EXPECT_EQ(bundle->info.numDicts, 0u);
    EXPECT_EQ(bundle->dicts, nullptr);
}

// --- LoadDict ---

TEST_F(CDictMgrTest, LoadSingleDict)
{
    auto dict = buildPackedDict(16, makeDictID(5), 42, 0xAB, true);

    auto r = CDictMgr_loadDict(&mgr_, dict.data(), dict.size());
    ASSERT_FALSE(ZL_RES_isError(r));

    ZL_DictID id             = makeDictID(5);
    ZL_MaterializerDesc2 mat = makeDefaultDictMaterializer();
    const ZL_Dict* found     = CDictMgr_findDict(&mgr_, &id, &mat);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->materializingCodec, 42u);
}

TEST_F(CDictMgrTest, LoadDictDuplicate)
{
    auto dict = buildPackedDict(16, makeDictID(7), 99, 0xAB, true);

    auto r1 = CDictMgr_loadDict(&mgr_, dict.data(), dict.size());
    ASSERT_FALSE(ZL_RES_isError(r1));
    auto r2 = CDictMgr_loadDict(&mgr_, dict.data(), dict.size());
    ASSERT_FALSE(ZL_RES_isError(r2));

    ZL_DictID id             = makeDictID(7);
    ZL_MaterializerDesc2 mat = makeDefaultDictMaterializer();
    EXPECT_NE(CDictMgr_findDict(&mgr_, &id, &mat), nullptr);
}

// --- FindDict ---

TEST_F(CDictMgrTest, FindDictNotLoaded)
{
    ZL_DictID unknownID = makeDictID(99);
    EXPECT_EQ(CDictMgr_findDict(&mgr_, &unknownID, NULL), nullptr);
}

TEST_F(CDictMgrTest, DictDeduplicationViaLoadDict)
{
    auto dict1 = buildPackedDict(10, makeDictID(1), 100, 0xAA, true);

    // Load same dict twice — second load should return the cached copy.
    auto r1 = CDictMgr_loadDict(&mgr_, dict1.data(), dict1.size());
    ASSERT_FALSE(ZL_RES_isError(r1));
    const ZL_Dict* first = ZL_RES_value(r1);

    auto r2 = CDictMgr_loadDict(&mgr_, dict1.data(), dict1.size());
    ASSERT_FALSE(ZL_RES_isError(r2));
    const ZL_Dict* second = ZL_RES_value(r2);

    EXPECT_EQ(first, second)
            << "same (dictID, materializer) should be deduplicated";
}

// --- Composite key: different materializer/codec type yields different entry
// ---

TEST(CDictMgrStandaloneTest, DifferentMaterializerYieldsDifferentEntry)
{
    // Set up mock nodes with dictID(1) and the default materializer.
    MockNodesMgr mockNodes;
    ZL_MaterializerDesc2 matA = makeDefaultDictMaterializer();
    mockNodes.addDictNode(makeDictID(1), matA, true /* standard node */);

    CDictMgr mgr{};
    ZL_Report r = CDictMgr_init(
            &mgr, mockNodes.nodesManager(), NULL, mockNodes.opCtx());
    ASSERT_FALSE(ZL_isError(r));

    auto customDict   = buildPackedDict(10, makeDictID(1), 100, 0xAA, true);
    auto standardDict = buildPackedDict(10, makeDictID(1), 100, 0xAA, false);

    // Load the dict — no matching custom CNode.
    auto lr = CDictMgr_loadDict(&mgr, customDict.data(), customDict.size());
    ASSERT_TRUE(ZL_RES_isError(lr));

    // Load the dict - matching standard CNode.
    lr = CDictMgr_loadDict(&mgr, standardDict.data(), standardDict.size());
    ASSERT_FALSE(ZL_RES_isError(lr));

    ZL_DictID id = makeDictID(1);

    // Lookup by (id, matA) — standard dict was loaded with matA.
    const ZL_Dict* dictA = CDictMgr_findDict(&mgr, &id, &matA);
    ASSERT_NE(dictA, nullptr);

    // Lookup with a different materializer → different composite key → miss.
    ZL_MaterializerDesc2 matB = {};
    const ZL_Dict* dictB      = CDictMgr_findDict(&mgr, &id, &matB);
    EXPECT_EQ(dictB, nullptr)
            << "different materializer should yield a different cache key";

    // Add custom CNode and load custom dict. Both use matA, so the cache
    // key (id, matA) is the same — the second load is a dedup/no-op.
    mockNodes.addDictNode(makeDictID(1), matA, false /* custom node */);
    lr = CDictMgr_loadDict(&mgr, customDict.data(), customDict.size());
    ASSERT_FALSE(ZL_RES_isError(lr));
    dictA = CDictMgr_findDict(&mgr, &id, &matA);
    ASSERT_NE(dictA, nullptr);

    CDictMgr_destroy(&mgr);
}

// --- GetBundleID ---

TEST_F(CDictMgrTest, GetBundleID)
{
    // No bundle ID before loading a bundle
    EXPECT_EQ(CDictMgr_getBundleID(&mgr_), nullptr);

    auto dict   = buildPackedDict(10, makeDictID(1), 100, 0xAB, true);
    auto fatBuf = packFatBundle({ dict });

    auto r = CDictMgr_loadFatBundle(&mgr_, fatBuf.data(), fatBuf.size());
    EXPECT_FALSE(ZL_RES_isError(r));

    const ZL_BundleID* bundleID = CDictMgr_getBundleID(&mgr_);
    ASSERT_NE(bundleID, nullptr);
    EXPECT_TRUE(ZL_UniqueID_isValid(&bundleID->id));
}
