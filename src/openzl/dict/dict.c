// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/dict/dict.h"

#include <stdint.h>
#include <string.h>

#include "openzl/common/unique_id.h"
#include "openzl/dict/dict_constants.h"
#include "openzl/fse/common/mem.h"

ZL_DictID Dict_extractID(const void* dictBuffer, size_t dictSize)
{
    ZL_ASSERT(dictSize >= ZL_DICT_HEADER_SIZE);

    ZL_DictID dictID;
    memcpy(dictID.id.bytes, (const unsigned char*)dictBuffer + 4, 32);
    return dictID;
}

ZL_RESULT_OF(ZL_ParsedDict) Dict_parse(const void* dictBuffer, size_t dictSize)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_ParsedDict, NULL);
    ZL_ERR_IF_NULL(dictBuffer, dict_corruption, "dict buffer must not be null");
    ZL_ERR_IF_LT(
            dictSize,
            ZL_DICT_HEADER_SIZE,
            dict_corruption,
            "dict buffer too small");

    const unsigned char* p = (const unsigned char*)dictBuffer;

    U32 const magic = MEM_readLE32(p);
    ZL_ERR_IF_NE(magic, ZL_DICT_MAGIC, dict_corruption, "invalid dict magic");
    p += 4;

    ZL_ParsedDict result;
    memset(&result, 0, sizeof(result));

    memcpy(result.dictID.id.bytes, p, 32);
    p += 32;

    result.materializingCodec = (ZL_IDType)MEM_readLE32(p);
    p += 4;

    result.codecType = (TransformType_e)(*p);
    p += 1;

    U32 const contentSize = MEM_readLE32(p);
    p += 4;

    ZL_ERR_IF_LT(
            dictSize,
            ZL_DICT_HEADER_SIZE + (size_t)contentSize,
            dict_corruption,
            "dict buffer truncated");

    result.contentSize = contentSize;
    result.packedSize  = ZL_DICT_HEADER_SIZE + (size_t)contentSize;
    result.dictContent = (const void*)p;
    result.contentHash = ZL_UniqueID_computeSHA256(p, contentSize);

    return ZL_RESULT_WRAP_VALUE(ZL_ParsedDict, result);
}

ZL_Report Dict_pack(
        void* dst,
        size_t dstCapacity,
        ZL_DictID dictID,
        ZL_IDType materializingCodec,
        TransformType_e codecType,
        const void* dictContent,
        size_t contentSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_GT(contentSize, UINT32_MAX, srcSize_tooLarge);

    size_t const packedSize = ZL_DICT_HEADER_SIZE + contentSize;

    ZL_ERR_IF_LT(dstCapacity, packedSize, dstCapacity_tooSmall);

    if (!ZL_UniqueID_isValid(&dictID.id)) {
        ZL_UniqueID const hash =
                ZL_UniqueID_computeSHA256(dictContent, contentSize);
        memcpy(&dictID.id, &hash, sizeof(dictID.id));
    }

    unsigned char* p = (unsigned char*)dst;

    MEM_writeLE32(p, ZL_DICT_MAGIC);
    p += 4;

    memcpy(p, dictID.id.bytes, 32);
    p += 32;

    MEM_writeLE32(p, (U32)materializingCodec);
    p += 4;

    *p = (unsigned char)codecType;
    p += 1;

    MEM_writeLE32(p, (U32)contentSize);
    p += 4;

    if (contentSize > 0) {
        memcpy(p, dictContent, contentSize);
    }

    return ZL_returnValue(packedSize);
}
