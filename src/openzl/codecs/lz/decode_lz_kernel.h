// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZS_DECODE_LZ_KERNEL_H
#define ZS_DECODE_LZ_KERNEL_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t* literals;
    size_t numLiterals;

    const void* offsets;
    size_t offsetsEltWidth;

    const void* literalLengths;
    size_t literalLengthsEltWidth;

    const void* matchLengths;
    size_t matchLengthsEltWidth;

    size_t numSequences;
} ZL_Lz_InSequences;

/// Detailed error codes for LZ decoding.
typedef enum {
    ZL_LzError_ok = 0,
    ZL_LzError_literalLengthTooLarge,
    ZL_LzError_notEnoughLiterals,
    ZL_LzError_offsetZero,
    ZL_LzError_offsetTooLarge,
    ZL_LzError_matchLengthTooLarge,
    ZL_LzError_literalsTooLarge,
    ZL_LzError_dstSizeTooLarge,
} ZL_LzError;

/**
 * Decodes LZ sequences back into the original byte stream.
 *
 * dst must have capacity for at least dstSize bytes.
 * Exactly dstSize bytes must be produced, otherwise an error is returned.
 *
 * The three numeric streams (offsets, literalLengths, matchLengths) may
 * have any element width (1, 2, 4, or 8 bytes), specified independently.
 *
 * @returns ZL_LzError_ok on success, or an error code upon corruption.
 */
ZL_LzError
ZL_Lz_decode(uint8_t* dst, size_t dstSize, const ZL_Lz_InSequences* src);

#endif
