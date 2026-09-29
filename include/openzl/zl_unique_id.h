// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_ZL_UNIQUE_ID_H
#define OPENZL_ZL_UNIQUE_ID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "openzl/zl_errors.h"
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

/**
 * @returns The number of significant leading bytes in @p id . Returns 0 if @p
 * id is all zeros.
 *
 * Example: for an ID whose bytes 0, 1, and 4 are non-zero and bytes 5-31 are
 * zero, this returns 5. This is useful for variable-length serialization where
 * trailing zero bytes can be omitted.
 */
size_t ZL_UniqueID_significantBytes(const ZL_UniqueID* id);

/**
 * Create a ZL_UniqueID from a 16-bit integer, stored in little-endian byte
 * order. Bytes 2-31 are zeroed.
 */
ZL_UniqueID ZL_UniqueID_fromU16(uint16_t value);

/**
 * Create a ZL_UniqueID from a 32-bit integer, stored in little-endian byte
 * order. Bytes 4-31 are zeroed.
 */
ZL_UniqueID ZL_UniqueID_fromU32(uint32_t value);

/**
 * Create a ZL_UniqueID from a 64-bit integer, stored in little-endian byte
 * order. Bytes 8-31 are zeroed.
 */
ZL_UniqueID ZL_UniqueID_fromU64(uint64_t value);

/**
 * Inverse of the fromXYZ functions. Returns error if parsing fails.
 */
ZL_RESULT_OF(uint16_t) ZL_UniqueID_toU16(const ZL_UniqueID* id);
ZL_RESULT_OF(uint32_t) ZL_UniqueID_toU32(const ZL_UniqueID* id);
ZL_RESULT_OF(uint64_t) ZL_UniqueID_toU64(const ZL_UniqueID* id);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OPENZL_ZL_UNIQUE_ID_H
