// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_DICT_UNIQUE_ID_H
#define OPENZL_DICT_UNIQUE_ID_H

#include <stdbool.h> // bool

#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Serialize a ZL_UniqueID into a raw byte buffer.
 * Writes exactly 32 bytes to @p dst.
 */
void ZL_UniqueID_write(void* dst, const ZL_UniqueID* id);

/**
 * Deserialize a ZL_UniqueID from a raw byte buffer.
 * Reads exactly 32 bytes from @p src into @p dst , no endianness conversion
 * required.
 */
void ZL_UniqueID_read(ZL_UniqueID* dst, const void* src);

/**
 * @returns An *invalid* ZL_UniqueID with all bytes zero.
 */
ZL_UniqueID ZL_UniqueID_zero(void);

/**
 * @returns true if @p id is non-NULL and not ZL_UniqueID_zero().
 */
bool ZL_UniqueID_isValid(const ZL_UniqueID* id);

/**
 * Returns a non-cryptographic hash (XXH3) of @p key, or 0 if @p key is NULL.
 */
size_t ZL_UniqueID_hash(const ZL_UniqueID* key);

bool ZL_UniqueID_eq(const ZL_UniqueID* lhs, const ZL_UniqueID* rhs);

/**
 * Compute the SHA-256 digest of @p data (of length @p size) and return it
 * as a ZL_UniqueID.
 */
ZL_UniqueID ZL_UniqueID_computeSHA256(const void* data, size_t size);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OPENZL_DICT_UNIQUE_ID_H
