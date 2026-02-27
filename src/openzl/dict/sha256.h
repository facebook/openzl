// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_DICT_SHA256_H
#define OPENZL_DICT_SHA256_H

#include <stdbool.h> // bool
#include <stddef.h>  // size_t

#include "openzl/zl_errors.h"

#if defined(__cplusplus)
extern "C" {
#endif

// ============================================================================
// ZL_SHA256 - SHA-256 Hash Type
// ============================================================================

/**
 * @brief SHA-256 hash type (32 bytes)
 * Used for dictionary uniqueness identification.
 */
typedef struct {
    unsigned char bytes[32];
} ZL_SHA256;

ZL_RESULT_DECLARE_TYPE(ZL_SHA256);

/**
 * "Hash" function for ZL_SHA256 (required for map usage). Returns the top
 * sizeof(size_t) bytes
 */
size_t ZL_SHA256_hash(const ZL_SHA256* key);

bool ZL_SHA256_eq(const ZL_SHA256* lhs, const ZL_SHA256* rhs);

/**
 * @brief Compute SHA-256 hash of data
 *
 * @param data Pointer to the data to hash
 * @param size Size of the data in bytes
 * @returns Computed SHA-256 hash
 */
ZL_SHA256 ZL_SHA256_compute(const void* data, size_t size);

/**
 * @brief Check if a SHA-256 hash is valid (non-zero)
 *
 * @param hash Pointer to the hash to check
 * @returns true if the hash is non-zero, false if all zeros
 */
bool ZL_SHA256_isValid(const ZL_SHA256* hash);

/**
 * @brief Get a zero-initialized SHA-256 hash
 *
 * @returns A SHA-256 hash with all bytes set to zero
 */
ZL_SHA256 ZL_SHA256_zero(void);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OPENZL_DICT_SHA256_H
