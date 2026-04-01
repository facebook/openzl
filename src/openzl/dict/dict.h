// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_DICT_DICT_H
#define OPENZL_DICT_DICT_H

#include "openzl/common/unique_id.h"
#include "openzl/common/wire_format.h"
#include "openzl/zl_errors.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Packed wire format for a serialized dictionary.
 *
 *   offset  size   field
 *   ------  -----  ------------------------------------------------
 *     0       4    magic       – 32-bit LE, must be 0x4944CCDAU
 *     4      32    id          – 256-bit LE ZL_DictID
 *    36       4    codec       – 32-bit LE materializing codec ID
 *    40       1    codecType   – 0 for standard, 1 for custom
 *    41       4    dictSize    – 32-bit LE length of content in bytes
 *    45       N    content     – dictSize bytes of raw dictionary content
 *
 *   Total packed size = 45 + dictSize
 */

struct ZL_Dict_s {
    ZL_DictID dictID;
    ZL_IDType materializingCodec;
    TransformType_e codecType;
    void* dictObj;
    size_t packedSize;
};

typedef struct {
    ZL_DictID dictID;
    ZL_IDType materializingCodec;
    TransformType_e codecType;
    const void* dictContent;
    size_t contentSize;
    size_t packedSize;
} ZL_ParsedDict;

ZL_RESULT_DECLARE_TYPE(ZL_ParsedDict);

/// Warning: The produced ZL_ParsedDict is non-owning. The dictContent field is
/// just a pointer to somewhere in the @p dictBuffer .
ZL_RESULT_OF(ZL_ParsedDict) Dict_parse(const void* dictBuffer, size_t dictSize);

/**
 * Packs dictionary content into a provided buffer.
 * @param dst the buffer to pack the dictionary into. Must be at least
 * 45 + @p contentSize bytes.
 * @param dstCapacity the size of the @p dst buffer in bytes.
 * @param dictID the dictionary ID. If ZL_DICT_ID_NULL is passed, the dictionary
 * ID will be automatically generated as the SHA256 of the dictionary content.
 *
 * @return an error if the buffer is too small. Otherwise, return the size of
 * the packed dictionary.
 */
ZL_Report Dict_pack(
        void* dst,
        size_t dstCapacity,
        ZL_DictID dictID,
        ZL_IDType materializingCodec,
        TransformType_e codecType,
        const void* dictContent,
        size_t contentSize);

/**
 * Extracts the ID from a packed dict. Assumes the buffer is a valid packed dict
 * and does no error checking.
 */
ZL_DictID Dict_extractID(const void* dictBuffer, size_t dictSize);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OPENZL_DICT_DICT_H
