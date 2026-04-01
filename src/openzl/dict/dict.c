// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/dict/dict.h"

#include <string.h>

#include "openzl/common/errors_internal.h"

const uint32_t ZL_DICT_MAGIC = 0x5A4C4449; // "ZLDI"

ZL_RESULT_OF(ZL_ParsedDict)
ZL_Dict_parse(
        ZL_OperationContext* opctx,
        const void* dictBlob,
        const size_t blobSize)
{
    ZL_ASSERT_NN(opctx);
    ZL_RESULT_DECLARE_SCOPE(ZL_ParsedDict, opctx);
    ZL_ERR_IF_NULL(dictBlob, dict_materialization, "dictBlob cannot be NULL");
    ZL_ERR_IF_LT(
            blobSize,
            3 * sizeof(uint32_t),
            dict_corruption,
            "dict blob must contain metadata fields");
    ZL_ERR_IF_NE(
            memcmp(dictBlob, &ZL_DICT_MAGIC, sizeof(ZL_DICT_MAGIC)),
            0,
            dict_corruption,
            "invalid dict magic");
    ZL_ParsedDict ret;
    ret.codecId  = ((const uint32_t*)dictBlob)[1];
    ret.dictSize = ((const uint32_t*)dictBlob)[2];
    ZL_ERR_IF_NE(
            blobSize,
            ret.dictSize + 3 * sizeof(uint32_t),
            dict_corruption,
            "Dict blob size mismatch");
    ret.rawDictContent = (const uint8_t*)dictBlob + 3 * sizeof(uint32_t);
    ret.hash           = ZL_SHA256_compute(ret.rawDictContent, ret.dictSize);
    return ZL_WRAP_VALUE(ret);
}
