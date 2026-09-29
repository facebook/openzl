// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_SHARED_BASE64_H
#define OPENZL_SHARED_BASE64_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/zl_errors.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Returns the number of bytes needed to hold the base64-encoded form
 * of @p srcSize bytes of input (without a null terminator).
 */
size_t ZL_base64EncodedSize(size_t srcSize);

/**
 * Base64-encode @p srcSize bytes from @p src into @p dst.
 *
 * @param dst      Destination buffer.
 * @param dstCap   Capacity of the destination buffer in bytes.
 * @param src      Source data to encode.
 * @param srcSize  Number of source bytes.
 *
 * @returns a ZL_Report containing the number of bytes written on success,
 *          or an error if @p dstCap is too small.
 */
ZL_Report
ZL_base64Encode(char* dst, size_t dstCap, const uint8_t* src, size_t srcSize);

/**
 * Decode a base64-encoded string.
 *
 * @param dst      Destination buffer for decoded bytes.
 * @param dstCap   Capacity of the destination buffer in bytes.
 * @param src      Base64-encoded input.
 * @param srcSize  Length of the input in bytes (must be a multiple of 4).
 *
 * @returns a ZL_Report containing the number of decoded bytes written on
 *          success, or an error on bad length, insufficient capacity, or
 * invalid characters.
 */
ZL_Report
ZL_base64Decode(uint8_t* dst, size_t dstCap, const char* src, size_t srcSize);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OPENZL_SHARED_BASE64_H
