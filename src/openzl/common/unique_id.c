// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/zl_unique_id.h"

#include <string.h>

#include "openzl/common/sha256.h"
#include "openzl/shared/mem.h" // ZL_writeLE16, ZL_writeLE32, ZL_writeLE64
#include "openzl/shared/xxhash.h"

void ZL_UniqueID_write(void* dst, const ZL_UniqueID* id)
{
    memcpy(dst, id->bytes, sizeof(id->bytes));
}

void ZL_UniqueID_read(ZL_UniqueID* dst, const void* src)
{
    memcpy(dst->bytes, src, sizeof(dst->bytes));
}

ZL_UniqueID ZL_UniqueID_zero(void)
{
    ZL_UniqueID result;
    memset(result.bytes, 0, sizeof(result.bytes));
    return result;
}

bool ZL_UniqueID_isValid(const ZL_UniqueID* id)
{
    if (id == NULL) {
        return false;
    }
    for (size_t i = 0; i < sizeof(id->bytes); i++) {
        if (id->bytes[i] != 0) {
            return true;
        }
    }
    return false;
}

size_t ZL_UniqueID_hash(const ZL_UniqueID* key)
{
    if (key == NULL) {
        return 0;
    }
    return XXH3_64bits(
            key->bytes, sizeof(key->bytes)); // no support for 32-bit archs
}

bool ZL_UniqueID_eq(const ZL_UniqueID* lhs, const ZL_UniqueID* rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return false;
    }
    return memcmp(lhs->bytes, rhs->bytes, sizeof(lhs->bytes)) == 0;
}

ZL_UniqueID ZL_UniqueID_computeSHA256(const void* data, size_t size)
{
    ZL_UniqueID result;
    ZL_sha256(data, size, result.bytes);
    return result;
}

size_t ZL_UniqueID_significantBytes(const ZL_UniqueID* id)
{
    for (size_t i = sizeof(id->bytes); i > 0; i--) {
        if (id->bytes[i - 1] != 0) {
            return i;
        }
    }
    return 0;
}

ZL_UniqueID ZL_UniqueID_fromU16(uint16_t value)
{
    ZL_UniqueID id;
    memset(id.bytes, 0, sizeof(id.bytes));
    ZL_writeLE16(id.bytes, value);
    return id;
}

ZL_UniqueID ZL_UniqueID_fromU32(uint32_t value)
{
    ZL_UniqueID id;
    memset(id.bytes, 0, sizeof(id.bytes));
    ZL_writeLE32(id.bytes, value);
    return id;
}

ZL_UniqueID ZL_UniqueID_fromU64(uint64_t value)
{
    ZL_UniqueID id;
    memset(id.bytes, 0, sizeof(id.bytes));
    ZL_writeLE64(id.bytes, value);
    return id;
}

ZL_RESULT_OF(uint16_t) ZL_UniqueID_toU16(const ZL_UniqueID* id)
{
    ZL_RESULT_DECLARE_SCOPE(uint16_t, NULL);
    ZL_ERR_IF_GT(
            ZL_UniqueID_significantBytes(id),
            sizeof(uint16_t),
            GENERIC,
            "ZL_UniqueID_toU16: id is not a 16-bit value");
    return ZL_WRAP_VALUE(ZL_readLE16(id->bytes));
}

ZL_RESULT_OF(uint32_t) ZL_UniqueID_toU32(const ZL_UniqueID* id)
{
    ZL_RESULT_DECLARE_SCOPE(uint32_t, NULL);
    ZL_ERR_IF_GT(
            ZL_UniqueID_significantBytes(id),
            sizeof(uint32_t),
            GENERIC,
            "ZL_UniqueID_toU32: id is not a 32-bit value");
    return ZL_WRAP_VALUE(ZL_readLE32(id->bytes));
}

ZL_RESULT_OF(uint64_t) ZL_UniqueID_toU64(const ZL_UniqueID* id)
{
    ZL_RESULT_DECLARE_SCOPE(uint64_t, NULL);
    ZL_ERR_IF_GT(
            ZL_UniqueID_significantBytes(id),
            sizeof(uint64_t),
            GENERIC,
            "ZL_UniqueID_toU64: id is not a 64-bit value");
    return ZL_WRAP_VALUE(ZL_readLE64(id->bytes));
}
