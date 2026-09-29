// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/dict/bundle.h"

#include <stddef.h>
#include <string.h>

#include "openzl/common/assertion.h"
#include "openzl/dict/dict.h"
#include "openzl/dict/dict_constants.h"
#include "openzl/fse/common/mem.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_unique_id.h"

static size_t bundleInfoPackedSize(size_t numDicts)
{
    return ZL_BUNDLE_HEADER_SIZE + numDicts * ZL_UNIQUE_ID_SIZE;
}

/* ================================================================
 * ZL_BundleInfo_parse
 * ================================================================ */

ZL_RESULT_OF(ZL_BundleInfo)
ZL_BundleInfo_parse(const void* bundleContent, size_t bundleSize)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_BundleInfo, NULL);
    ZL_ERR_IF_NULL(
            bundleContent, dict_corruption, "bundle buffer must not be null");
    ZL_ERR_IF_LT(
            bundleSize,
            ZL_BUNDLE_HEADER_SIZE,
            dict_corruption,
            "bundle buffer too small");

    const unsigned char* p = (const unsigned char*)bundleContent;

    U32 const magic = MEM_readLE32(p);
    ZL_ERR_IF_NE(
            magic,
            ZL_BUNDLEINFO_MAGIC,
            dict_corruption,
            "invalid bundle magic");
    p += 4;

    ZL_UniqueID bundleUID;
    ZL_UniqueID_read(&bundleUID, p);
    p += ZL_UNIQUE_ID_SIZE;

    unsigned char const isFatBundle = *p;
    p += 1;

    U32 const numDicts = MEM_readLE32(p);
    p += 4;

    ZL_ERR_IF_GT(
            numDicts,
            ZL_MAX_DICTS_PER_BUNDLE,
            dict_corruption,
            "numDicts too large");

    size_t const dictArraySize = (size_t)numDicts * ZL_UNIQUE_ID_SIZE;
    ZL_ERR_IF_LT(
            bundleSize,
            ZL_BUNDLE_HEADER_SIZE + dictArraySize,
            dict_corruption,
            "bundle buffer truncated");

    ZL_BundleInfo result;
    memset(&result, 0, sizeof(result));
    result.bundleID.id = bundleUID;
    result.isFatBundle = (bool)isFatBundle;
    result.numDicts    = numDicts;
    result.dictIDs     = (numDicts > 0) ? (const ZL_DictID*)p : NULL;
    result.packedSize  = ZL_BUNDLE_HEADER_SIZE + dictArraySize;

    return ZL_RESULT_WRAP_VALUE(ZL_BundleInfo, result);
}

/* ================================================================
 * BundleInfo_pack
 * ================================================================ */

ZL_Report
BundleInfo_pack(void* dst, size_t dstCapacity, const ZL_BundleInfo* info)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_GT(info->numDicts, ZL_MAX_DICTS_PER_BUNDLE, srcSize_tooLarge);

    size_t const packedSize = bundleInfoPackedSize(info->numDicts);

    ZL_ERR_IF_LT(dstCapacity, packedSize, dstCapacity_tooSmall);

    unsigned char* p = (unsigned char*)dst;

    MEM_writeLE32(p, ZL_BUNDLEINFO_MAGIC);
    p += 4;

    ZL_UniqueID_write(p, &info->bundleID.id);
    p += ZL_UNIQUE_ID_SIZE;

    *p = (unsigned char)info->isFatBundle;
    p += 1;

    MEM_writeLE32(p, (U32)info->numDicts);
    p += 4;

    for (size_t i = 0; i < info->numDicts; i++) {
        ZL_UniqueID_write(p, &info->dictIDs[i].id);
        p += ZL_UNIQUE_ID_SIZE;
    }

    return ZL_returnValue(packedSize);
}

ZL_BundleID ZL_DictBundle_genBundleID(const ZL_DictID* dictIDs, size_t numDicts)
{
    ZL_ASSERT(dictIDs != NULL || numDicts == 0);
    ZL_BundleID result;
    result.id =
            ZL_UniqueID_computeSHA256(dictIDs, numDicts * ZL_UNIQUE_ID_SIZE);
    return result;
}

/* ================================================================
 * ZL_DictBundle_packFatBundle
 * ================================================================ */

ZL_Report ZL_DictBundle_packFatBundle(
        void* dst,
        size_t dstCapacity,
        const void** dicts,
        size_t* dictSizes,
        size_t numDicts)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_GT(numDicts, ZL_MAX_DICTS_PER_BUNDLE, srcSize_tooLarge);

    size_t totalDictSize = 0;
    for (size_t i = 0; i < numDicts; i++) {
        size_t const prev = totalDictSize;
        totalDictSize += dictSizes[i];
        ZL_ERR_IF_LT(totalDictSize, prev, srcSize_tooLarge);
    }

    size_t const infoSize  = bundleInfoPackedSize(numDicts);
    size_t const totalSize = infoSize + totalDictSize;
    // overflow check
    ZL_ERR_IF_LT(totalSize, totalDictSize, srcSize_tooLarge);
    ZL_ERR_IF_LT(totalSize, infoSize, srcSize_tooLarge);

    ZL_ERR_IF_LT(dstCapacity, totalSize, dstCapacity_tooSmall);

    unsigned char* p = (unsigned char*)dst;

    /* Write BundleInfo header with a zeroed bundleID placeholder */
    MEM_writeLE32(p, ZL_BUNDLEINFO_MAGIC);
    p += 4;

    unsigned char* const bundleIDSlot = p;
    memset(p, 0, ZL_UNIQUE_ID_SIZE);
    p += ZL_UNIQUE_ID_SIZE;

    *p = 1; /* isFatBundle = true */
    p += 1;

    MEM_writeLE32(p, (U32)numDicts);
    p += 4;

    /* Reach into each packed dict to fetch the dictID */
    for (size_t i = 0; i < numDicts; i++) {
        ZL_DictID dictID = Dict_extractID(dicts[i], dictSizes[i]);
        ZL_UniqueID_write(p, &dictID.id);
        p += ZL_UNIQUE_ID_SIZE;
    }

    /* Generate bundleID as SHA256 of the dictID array */
    ZL_DictID* const dictIDArray =
            (ZL_DictID*)(bundleIDSlot + ZL_UNIQUE_ID_SIZE + 1 + 4);
    ZL_BundleID const bundleHash =
            ZL_DictBundle_genBundleID(dictIDArray, numDicts);
    ZL_UniqueID_write(bundleIDSlot, &bundleHash.id);

    /* Append each packed dict blob */
    for (size_t i = 0; i < numDicts; i++) {
        memcpy(p, dicts[i], dictSizes[i]);
        p += dictSizes[i];
    }

    return ZL_returnValue(totalSize);
}
