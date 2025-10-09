// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_LZ_COMMON_FIELD_LZ_H
#define ZSTRONG_TRANSFORMS_LZ_COMMON_FIELD_LZ_H

#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

typedef struct {
    // field-size literals
    void* literalElts;
    size_t nbLiteralElts;
    size_t literalEltsCapacity;

    uint16_t* tokens;
    size_t nbTokens;

    uint32_t* offsets;
    size_t nbOffsets;

    uint32_t* extraLiteralLengths;
    size_t nbExtraLiteralLengths;

    uint32_t* extraMatchLengths;
    size_t nbExtraMatchLengths;

    size_t sequencesCapacity;
} ZL_FieldLz_OutSequences;

size_t ZL_FieldLz_maxNbSequences(size_t nbElts, size_t eltWidth);

/**
 * Allows allocating scratch memory for the compressor.
 * This cleanup is assumed to be handled by the caller.
 * E.g. Handled by Zstrong when used inside of a transform.
 */
typedef struct {
    void* (*alloc)(void* opaque, size_t size);
    void* opaque;
} ZL_FieldLz_Allocator;

ZL_Report ZL_FieldLz_compress(
        ZL_FieldLz_OutSequences* dst,
        void const* src,
        size_t nbElts,
        size_t eltWidth,
        int level,
        ZL_FieldLz_Allocator allocator);

typedef struct {
    // field-size literals
    void const* literalElts;
    size_t nbLiteralElts;

    uint16_t const* tokens;
    size_t nbTokens;

    uint32_t const* offsets;
    size_t nbOffsets;

    uint32_t const* extraLiteralLengths;
    size_t nbExtraLiteralLengths;

    uint32_t const* extraMatchLengths;
    size_t nbExtraMatchLengths;
} ZL_FieldLz_InSequences;

ZL_Report ZL_FieldLz_decompress(
        void* dst,
        size_t dstEltCapacity,
        size_t eltWidth,
        ZL_FieldLz_InSequences const* src);

// Details

#define kMinMatch(fieldSize) \
    (((fieldSize) == 1) ? 4 : (((fieldSize) == 2) ? 2 : 1))

#define kTokenOFBits 2
#define kTokenLLBits 4
#define kTokenMLBits 4

#define kTokenOFMask ((1 << kTokenOFBits) - 1)
#define kTokenLLMask ((1 << kTokenLLBits) - 1)
#define kTokenMLMask ((1 << kTokenMLBits) - 1)

#define kMaxLitLengthCode ((1u << kTokenLLBits) - 1)
#define kMaxMatchLengthCode ((1u << kTokenMLBits) - 1)

ZL_END_C_DECLS

#endif
