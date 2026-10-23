// Copyright (c) Meta Platforms, Inc. and affiliates.

// note : this is the raw kernel transform,
//        which implies minimal #include policy
#include "decode_splitByStruct_kernel.h"

#include <assert.h>
#include <string.h> // memcpy

static size_t sumArrayST(const size_t array[], size_t arraySize)
{
    size_t total = 0;
    if (arraySize) {
        assert(array != NULL);
    }
    for (size_t n = 0; n < arraySize; n++) {
        total += array[n];
    }
    return total;
}

// Variant dedicated to all @fieldSizes <= 8
// Note : in the future more variant could be created for other sizes,
// typically 4 and 16, maybe 2 and 1, possibly 32, etc.
static size_t rejoin_max8(
        void* restrict dst,
        size_t dstCapacity,
        const void* restrict srcs[],
        const size_t fieldSizes[],
        size_t nbFields,
        const size_t nbElts)
{
    if (dstCapacity) {
        assert(dst != NULL);
    }
    if (!nbElts) {
        return 0;
    }
    assert(srcs != NULL);
    assert(fieldSizes != NULL);
    assert(nbFields);
    size_t minSize = (size_t)(-1);
    for (size_t n = 0; n < nbFields; n++) {
        assert(srcs[n] != NULL);
        assert(fieldSizes[n] <= 8);
        if (fieldSizes[n] < minSize) {
            minSize = fieldSizes[n];
        }
    }
    assert(minSize > 0);
    size_t const nbSafeRounds_precalc[8] = { 8, 4, 3, 2, 2, 2, 2, 1 };
    size_t const nbSafeRounds            = nbSafeRounds_precalc[minSize - 1];

    size_t const structSize = sumArrayST(fieldSizes, nbFields);
    size_t const dstSize    = structSize * nbElts;
    assert(dstSize <= dstCapacity);

    size_t wPos     = 0;
    size_t structNb = 0;
    if (nbElts > nbSafeRounds) {
        for (; structNb < (nbElts - nbSafeRounds); structNb++) {
            for (size_t f = 0; f < nbFields; f++) {
                size_t const fs = fieldSizes[f];
                memcpy((char*)dst + wPos,
                       (const char*)srcs[f] + structNb * fs,
                       8);
                wPos += fs;
            }
        }
    }
    // Finalize, using exact field sizes
    for (; structNb < nbElts; structNb++) {
        for (size_t f = 0; f < nbFields; f++) {
            size_t const fs = fieldSizes[f];
            memcpy((char*)dst + wPos, (const char*)srcs[f] + structNb * fs, fs);
            wPos += fs;
        }
    }
    assert(wPos == dstSize);
    return dstSize;
}

/// Copies @p nbElts fields of @p fieldSize bytes from @p src to @p dst, one
/// every @p stride bytes. Common sizes are copied with constant-size memcpy().
static void copyField(
        char* restrict dst,
        size_t stride,
        const char* restrict src,
        size_t fieldSize,
        size_t nbElts)
{
#define ZS_COPY_FIELD(size)                                 \
    for (size_t e = 0; e < nbElts; e++) {                   \
        memcpy(dst + e * stride, src + e * (size), (size)); \
    }
    switch (fieldSize) {
        case 1:
            ZS_COPY_FIELD(1);
            break;
        case 2:
            ZS_COPY_FIELD(2);
            break;
        case 4:
            ZS_COPY_FIELD(4);
            break;
        case 8:
            ZS_COPY_FIELD(8);
            break;
        case 12:
            ZS_COPY_FIELD(12);
            break;
        case 16:
            ZS_COPY_FIELD(16);
            break;
        case 32:
            ZS_COPY_FIELD(32);
            break;
        case 64:
            ZS_COPY_FIELD(64);
            break;
        case 128:
            ZS_COPY_FIELD(128);
            break;
        default:
            ZS_COPY_FIELD(fieldSize);
            break;
    }
#undef ZS_COPY_FIELD
}

/// Rejoins the fields one by one, over blocks of structures small enough for
/// the block of @p dst to stay in cache between fields.
static void rejoin_byField(
        void* restrict dst,
        const void* restrict srcs[],
        const size_t fieldSizes[],
        size_t nbFields,
        size_t structSize,
        size_t nbElts)
{
    if (structSize == 0) {
        return;
    }
    size_t const blockElts = structSize < 16384 ? 16384 / structSize : 1;
    for (size_t start = 0; start < nbElts; start += blockElts) {
        size_t const n =
                nbElts - start < blockElts ? nbElts - start : blockElts;
        size_t offset = 0;
        for (size_t f = 0; f < nbFields; f++) {
            copyField(
                    (char*)dst + start * structSize + offset,
                    structSize,
                    (const char*)srcs[f] + start * fieldSizes[f],
                    fieldSizes[f],
                    n);
            offset += fieldSizes[f];
        }
    }
}

size_t ZS_dispatchArrayFixedSizeStruct_decode(
        void* restrict dst,
        size_t dstCapacity,
        const void* restrict srcs[],
        const size_t fieldSizes[],
        size_t nbFields,
        const size_t nbElts)
{
    if (dstCapacity) {
        assert(dst != NULL);
    }
    if (!nbElts) {
        return 0;
    }
    assert(srcs != NULL);
    assert(fieldSizes != NULL);
    assert(nbFields);
    size_t maxFieldSize = 0;
    size_t minFieldSize = (size_t)(-1);
    for (size_t n = 0; n < nbFields; n++) {
        assert(srcs[n] != NULL);
        if (fieldSizes[n] > maxFieldSize) {
            maxFieldSize = fieldSizes[n];
        }
        if (fieldSizes[n] < minFieldSize) {
            minFieldSize = fieldSizes[n];
        }
    }
    size_t const structSize = sumArrayST(fieldSizes, nbFields);
    size_t const dstSize    = structSize * nbElts;
    assert(dstSize <= dstCapacity);

    // Shortcut to 8-bytes variant
    if (maxFieldSize <= 8 && minFieldSize >= 1) {
        return rejoin_max8(
                dst, dstCapacity, srcs, fieldSizes, nbFields, nbElts);
    }

    rejoin_byField(dst, srcs, fieldSizes, nbFields, structSize, nbElts);
    return dstSize;
}
