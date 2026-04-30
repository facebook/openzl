// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/dict/bundle.h"

#include <stddef.h>
#include <string.h>

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
    unsigned char* const dictIDArray = bundleIDSlot + ZL_UNIQUE_ID_SIZE + 1 + 4;
    ZL_UniqueID const bundleHash     = ZL_UniqueID_computeSHA256(
            dictIDArray, numDicts * ZL_UNIQUE_ID_SIZE);
    ZL_UniqueID_write(bundleIDSlot, &bundleHash);

    /* Append each packed dict blob */
    for (size_t i = 0; i < numDicts; i++) {
        memcpy(p, dicts[i], dictSizes[i]);
        p += dictSizes[i];
    }

    return ZL_returnValue(totalSize);
}

/* ================================================================
 * ZL_DictBundle_parseFatBundle
 * ================================================================ */

ZL_RESULT_OF(ZL_DictBundleConstPtr)
ZL_DictBundle_parseFatBundle(
        const void* bundleContent,
        size_t bundleSize,
        Arena* allocator)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_DictBundleConstPtr, NULL);
    /* Allocate the bundle struct */
    ZL_DictBundle* bundle = (ZL_DictBundle*)ALLOC_Arena_malloc(
            allocator, sizeof(ZL_DictBundle));
    ZL_ERR_IF_NULL(bundle, allocation);

    ZL_RESULT_OF(ZL_BundleInfo)
    infoResult = ZL_BundleInfo_parse(bundleContent, bundleSize);
    ZL_ERR_IF_ERR(infoResult);
    bundle->info = ZL_RES_value(infoResult);
    if (bundle->info.numDicts != 0) {
        ZL_DictID* dictIDs = (ZL_DictID*)ALLOC_Arena_malloc(
                allocator, bundle->info.numDicts * sizeof(ZL_DictID));
        ZL_ERR_IF_NULL(dictIDs, allocation);
        memcpy(dictIDs,
               bundle->info.dictIDs,
               bundle->info.numDicts * sizeof(ZL_DictID));
        bundle->info.dictIDs = dictIDs;
    }

    ZL_ERR_IF_NE(
            (int)bundle->info.isFatBundle,
            1,
            dict_corruption,
            "expected isFatBundle=1 for fat bundle");

    /* Allocate the dicts pointer array */
    if (bundle->info.numDicts > 0) {
        bundle->dicts = (const ZL_Dict**)ALLOC_Arena_malloc(
                allocator, bundle->info.numDicts * sizeof(const ZL_Dict*));
        ZL_ERR_IF_NULL(
                bundle->dicts,
                allocation,
                "failed to allocate dicts array for %zu dicts",
                bundle->info.numDicts);
    } else {
        bundle->dicts = NULL;
    }

    size_t totalConsumed = bundle->info.packedSize;
    size_t remaining     = bundleSize - totalConsumed;
    const unsigned char* p =
            (const unsigned char*)bundleContent + totalConsumed;

    for (size_t i = 0; i < bundle->info.numDicts; i++) {
        ZL_RESULT_OF(ZL_ParsedDict) dictResult = ZL_Dict_parse(p, remaining);
        ZL_ERR_IF_ERR(dictResult);

        ZL_ParsedDict const parsed = ZL_RES_value(dictResult);

        /* Validate that this dict's ID matches the declared ID in the info */
        ZL_ERR_IF_NOT(
                ZL_UniqueID_eq(&parsed.dictID.id, &bundle->info.dictIDs[i].id),
                dict_corruption,
                "fat bundle dict[%zu] ID does not match declared dictID",
                i);

        /* Allocate and populate a ZL_Dict from the parsed wire data */
        ZL_Dict* dict =
                (ZL_Dict*)ALLOC_Arena_calloc(allocator, sizeof(ZL_Dict));
        ZL_ERR_IF_NULL(dict, allocation, "failed to allocate ZL_Dict[%zu]", i);
        dict->dictID             = parsed.dictID;
        dict->materializingCodec = parsed.materializingCodec;
        dict->isCustomCodec      = parsed.isCustomCodec;
        dict->packedSize         = parsed.packedSize;

        size_t allocSize = (parsed.contentSize == 0) ? 1 : parsed.contentSize;
        void* dictObj    = ALLOC_Arena_malloc(allocator, allocSize);
        ZL_ERR_IF_NULL(dictObj, allocation);
        memcpy(dictObj, parsed.dictContent, parsed.contentSize);
        dict->dictObj    = dictObj; // TODO: materialize
        bundle->dicts[i] = dict;

        size_t const dictWireSize = parsed.packedSize;
        p += dictWireSize;
        remaining -= dictWireSize;
        totalConsumed += dictWireSize;
    }
    bundle->packedSize = totalConsumed;

    return ZL_RESULT_WRAP_VALUE(ZL_DictBundleConstPtr, bundle);
}
