// Copyright (c) Meta Platforms, Inc. and affiliates.

// note : relative-path include (same directory by default)
#include "encode_delta_kernel.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h> // uint32_t, uint64_t
#include <string.h>

#include "openzl/shared/mem.h"

// Note: in the best world,
// I would expect some checkable assumptions to be asserted below,
// such as ensuring ptrs are non NULL when @nelts > 0.
// However, I don't want to have `<assert.h>` in competition with zstrong's
// <assertion.h>, and on the other hand, I want to keep this file free of
// zstrong specific includes for transportability and swappability purposes.

void ZS_deltaEncode64(
        uint64_t* first,
        uint64_t* deltas,
        uint64_t const* src,
        size_t nelts)
{
    if (!nelts) {
        return;
    }
    *first = src[0];
    for (size_t n = 1; n < nelts; ++n) {
        deltas[n - 1] = src[n] - src[n - 1];
    }
}

void ZS_deltaEncode32(
        uint32_t* first,
        uint32_t* deltas,
        uint32_t const* src,
        size_t nelts)
{
    if (!nelts) {
        return;
    }
    *first = src[0];
    for (size_t n = 1; n < nelts; ++n) {
        deltas[n - 1] = src[n] - src[n - 1];
    }
}

void ZS_deltaEncode16(
        uint16_t* first,
        uint16_t* deltas,
        uint16_t const* src,
        size_t nelts)
{
    if (!nelts) {
        return;
    }
    *first = src[0];
    for (size_t n = 1; n < nelts; ++n) {
        deltas[n - 1] = (uint16_t)(src[n] - src[n - 1]);
    }
}

void ZS_deltaEncode8(
        uint8_t* first,
        uint8_t* deltas,
        uint8_t const* src,
        size_t nelts)
{
    if (!nelts) {
        return;
    }
    *first = src[0];
    for (size_t n = 1; n < nelts; ++n) {
        deltas[n - 1] = (uint8_t)(src[n] - src[n - 1]);
    }
}

void ZS_deltaEncode(
        void* first,
        void* deltas,
        void const* src,
        size_t nbElts,
        size_t eltWidth)
{
    if (nbElts == 0) {
        return;
    }
    switch (eltWidth) {
        case 1: {
            uint8_t firstU8;
            ZS_deltaEncode8(
                    &firstU8, (uint8_t*)deltas, (uint8_t const*)src, nbElts);
            memcpy(first, &firstU8, sizeof(firstU8));
            break;
        }
        case 2: {
            uint16_t firstU16;
            ZS_deltaEncode16(
                    &firstU16, (uint16_t*)deltas, (uint16_t const*)src, nbElts);
            ZL_writeLE16(first, firstU16);
            break;
        }
        case 4: {
            uint32_t firstU32;
            ZS_deltaEncode32(
                    &firstU32, (uint32_t*)deltas, (uint32_t const*)src, nbElts);
            ZL_writeLE32(first, firstU32);
            break;
        }
        case 8: {
            uint64_t firstU64;
            ZS_deltaEncode64(
                    &firstU64, (uint64_t*)deltas, (uint64_t const*)src, nbElts);
            ZL_writeLE64(first, firstU64);
            break;
        }
        default:
            assert(false);
            break;
    }
}
