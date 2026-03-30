// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/common/allocation.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"

#include "Dict.h"

using namespace ::testing;

TEST(DictE2E, bundleOSS)
{
    ZL_Compressor* compressor; // pre-existing
    std::string infoStr               = "bundle info";
    std::vector<std::string> dictStrs = { "dict1", "dict2" };

    // Default dict behavior. Creates a new arena for managed allocations
    ZL_StaticSetDictLoader dictLoader(infoStr, dictStrs);
    // (Optional) check dicts
    ZL_DictBundleID bundleID = ZL_Compressor_getDictBundleID(compressor);
    ZL_DictBundle* bundle = ZL_RES_value(dictLoader.fetchDictBundle(bundleID));
    for (size_t i = 0; i < ZL_DictBundle_numDicts(bundle); ++i) {
        ZL_DictID dictID = ZL_DictBundle_dictIDs(bundle)[i];
        // Example: pre-fetch dict to ensure availability
        ZL_REQUIRE_SUCCESS(dictLoader.fetchDict(&dictID, compressor));
    }
    ZL_CCtx* cctx;
    ZL_CCtx_refDictLoader(cctx, dictLoader);
}

TEST(DictE2E, bundleMC)
{
    std::vector<ZL_Compressor*> compressorPool;
    std::map<ZL_DictBundleID, std::string>
            bundlesInCfg;                        // simulate configerator
    std::map<ZL_DictID, std::string> dictsInCfg; // simulate configerator

    ZL_DictLoader globalStore; // custom dict loader
    for (auto* comp : compressorPool) {
        ZL_DictBundleID bundleID = ZL_Compressor_getDictBundleID(comp);
        if (!ZL_SHA256_isValid(&bundleID)) {
            // no dicts for this compressor
            continue;
        }
        if (globalStore.hasBundle(bundleID)) { // MC-specific function
            // no need to double-materialize
            continue;
        }
        // If we are unable to materialize all dicts, that's an error
        ZL_REQUIRE_SUCCESS(globalStore.prefetchDictsForBundle(
                bundleID, comp)); // MC-specific function
    }
}

TEST(DictE2E, bundleDecomp)
{
    ZL_DCtx* dctx;
    std::map<ZL_SHA256, std::string> bundlesInCfg; // simulate configerator
    std::map<ZL_SHA256, std::string> dictsInCfg;   // simulate configerator
    MCDictLoader globalStore;                      // one per process

    std::vector<std::string> compressedBlobs = { "blob1", "blob2" };

    ZL_DCtx_refDictLoader(dctx, globalStore);
    for (auto& blob : compressedBlobs) {
        // This is an illustration of a hypothetical call flow. In practice, the
        // user would simply call:
        // ZL_DCtx_refDictLoader(dctx, globalStore);
        // ZL_DCtx_decompress(dctx, blob.data(), blob.size());
        ZL_DictBundleID bundleID =
                DCtx_getBundleIDFromFrame(dctx, blob.data(), blob.size());
        auto res = globalStore.fetchDictBundle(bundleID);
        ZL_ERR_IF_ERR(res);
        ZL_DictBundle* bundle = ZL_RES_value(res);
        ZL_DictID dictIDs[]   = ZL_DictBundle_dictIDs(bundle);
        std::vector<int> localIDsFromFrame;
        for (auto idx : localIDsFromFrame) {
            ZL_DictID dictID = dictIDs[idx];
            auto dictRes     = globalStore.fetchDict(dictID);
            ZL_ERR_IF_ERR(dictRes);
            ZL_Dict* dict = ZL_RES_value(dictRes);
            // do something with the dict
        }
    }
}
