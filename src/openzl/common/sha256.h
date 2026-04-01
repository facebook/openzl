// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_COMMON_SHA256_H
#define OPENZL_COMMON_SHA256_H

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Compute the SHA-256 digest of @p data (of length @p len) and write the
 * 32-byte result into @p digest.
 */
void ZL_sha256(const unsigned char* data, size_t len, unsigned char* digest);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // OPENZL_COMMON_SHA256_H
