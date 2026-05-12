// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/zl_unique_id.h"

#include <string.h>

#include "openzl/common/sha256.h"
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
