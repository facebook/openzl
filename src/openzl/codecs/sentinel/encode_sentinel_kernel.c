// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/sentinel/encode_sentinel_kernel.h"

#include <assert.h>

#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/utils.h"

ZL_FORCE_INLINE size_t ZL_sentinelByteEncode_impl(
        uint8_t* values,
        void* exceptions,
        const void* input,
        size_t numElts,
        size_t kEltWidth)
{
    const uint8_t* const inPtr = (const uint8_t*)input;
    uint8_t* const excPtr      = (uint8_t*)exceptions;

    size_t numExceptions = 0;
    for (size_t i = 0; i < numElts; ++i) {
        const uint64_t v = ZL_readN(inPtr + i * kEltWidth, kEltWidth);
        if (ZL_LIKELY(v < 255)) {
            values[i] = (uint8_t)v;
        } else {
            values[i] = 255;
            ZL_writeN(excPtr + numExceptions * kEltWidth, v, kEltWidth);
            ++numExceptions;
        }
    }
    return numExceptions;
}

size_t ZL_sentinelByteEncode(
        uint8_t* values,
        void* exceptions,
        const void* input,
        size_t numElts,
        size_t eltWidth)
{
    assert(eltWidth == 2 || eltWidth == 4 || eltWidth == 8);
    switch (eltWidth) {
        case 2:
            return ZL_sentinelByteEncode_impl(
                    values, exceptions, input, numElts, 2);
        case 4:
            return ZL_sentinelByteEncode_impl(
                    values, exceptions, input, numElts, 4);
        case 8:
            return ZL_sentinelByteEncode_impl(
                    values, exceptions, input, numElts, 8);
    }
    assert(false);
    return 0;
}

int ZL_sentinelEncode(
        void* values,
        void* exceptions,
        const void* input,
        size_t numElts,
        size_t eltWidth,
        const size_t* exceptionIndices,
        size_t numExceptions,
        uint64_t sentinel)
{
    assert(eltWidth == 1 || eltWidth == 2 || eltWidth == 4 || eltWidth == 8);
    assert((sentinel & ZL_maxValueForWidth(eltWidth)) == sentinel);

    const uint8_t* const inPtr = (const uint8_t*)input;
    uint8_t* const valPtr      = (uint8_t*)values;
    uint8_t* const excPtr      = (uint8_t*)exceptions;

    size_t excIdx = 0;
    for (size_t i = 0; i < numElts; ++i) {
        const uint64_t v = ZL_readN(inPtr + i * eltWidth, eltWidth);
        if (excIdx < numExceptions && exceptionIndices[excIdx] == i) {
            ZL_writeN(valPtr + i * eltWidth, sentinel, eltWidth);
            ZL_writeN(excPtr + excIdx * eltWidth, v, eltWidth);
            ++excIdx;
        } else {
            if (v == sentinel) {
                return 1; /* validation failure: sentinel in non-exception
                             position */
            }
            ZL_writeN(valPtr + i * eltWidth, v, eltWidth);
        }
    }

    /* All exception indices must have been consumed */
    if (excIdx != numExceptions) {
        return 2; /* validation failure: out-of-range indices */
    }

    return 0;
}
