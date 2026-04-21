// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/sentinel/decode_sentinel_kernel.h"

#include <assert.h>

#include "openzl/shared/mem.h"

ZL_FORCE_INLINE int ZL_sentinelDecode_impl(
        void* output,
        const void* values,
        const void* exceptions,
        size_t numElts,
        size_t numExceptions,
        size_t kValWidth,
        size_t kOutWidth,
        uint64_t sentinel)
{
    const uint8_t* const valPtr = (const uint8_t*)values;
    const uint8_t* const excPtr = (const uint8_t*)exceptions;
    uint8_t* const outPtr       = (uint8_t*)output;

    size_t excIdx = 0;
    for (size_t i = 0; i < numElts; ++i) {
        const uint64_t v = ZL_readN(valPtr + i * kValWidth, kValWidth);
        if (ZL_LIKELY(v != sentinel)) {
            /* zero-extend from valWidth to outWidth */
            ZL_writeN(outPtr + i * kOutWidth, v, kOutWidth);
        } else {
            if (excIdx >= numExceptions) {
                return 1; /* corruption: not enough exceptions */
            }
            const uint64_t exc =
                    ZL_readN(excPtr + excIdx * kOutWidth, kOutWidth);
            ZL_writeN(outPtr + i * kOutWidth, exc, kOutWidth);
            ++excIdx;
        }
    }

    if (excIdx != numExceptions) {
        return 2; /* corruption: unconsumed exceptions */
    }

    return 0;
}

static int ZL_sentinelDecode_generic(
        void* output,
        const void* values,
        const void* exceptions,
        size_t numElts,
        size_t numExceptions,
        size_t valWidth,
        size_t outWidth,
        uint64_t sentinel)
{
    return ZL_sentinelDecode_impl(
            output,
            values,
            exceptions,
            numElts,
            numExceptions,
            valWidth,
            outWidth,
            sentinel);
}

#define ZL_GEN_SENTINEL_DECODE_IMPL(v, o)   \
    static int ZL_sentinelDecode_##v##_##o( \
            void* output,                   \
            const void* values,             \
            const void* exceptions,         \
            size_t numElts,                 \
            size_t numExceptions,           \
            uint64_t sentinel)              \
    {                                       \
        return ZL_sentinelDecode_impl(      \
                output,                     \
                values,                     \
                exceptions,                 \
                numElts,                    \
                numExceptions,              \
                v,                          \
                o,                          \
                sentinel);                  \
    }

ZL_GEN_SENTINEL_DECODE_IMPL(1, 2)
ZL_GEN_SENTINEL_DECODE_IMPL(1, 4)
ZL_GEN_SENTINEL_DECODE_IMPL(1, 8)

ZL_GEN_SENTINEL_DECODE_IMPL(1, 1)
ZL_GEN_SENTINEL_DECODE_IMPL(2, 2)
ZL_GEN_SENTINEL_DECODE_IMPL(4, 4)
ZL_GEN_SENTINEL_DECODE_IMPL(8, 8)

int ZL_sentinelDecode(
        void* output,
        const void* values,
        const void* exceptions,
        size_t numElts,
        size_t numExceptions,
        size_t valWidth,
        size_t outWidth,
        uint64_t sentinel)
{
    assert(valWidth == 1 || valWidth == 2 || valWidth == 4 || valWidth == 8);
    assert(outWidth == 1 || outWidth == 2 || outWidth == 4 || outWidth == 8);
    assert(valWidth <= outWidth);

#define ZL_SENTINEL_DECODE_IMPL(v, o) \
    ZL_sentinelDecode_##v##_##o(      \
            output, values, exceptions, numElts, numExceptions, sentinel)

    if (valWidth == 1 && outWidth > 1) {
        switch (outWidth) {
            case 2:
                return ZL_SENTINEL_DECODE_IMPL(1, 2);
            case 4:
                return ZL_SENTINEL_DECODE_IMPL(1, 4);
            case 8:
                return ZL_SENTINEL_DECODE_IMPL(1, 8);
        }
        assert(false);
    } else if (valWidth == outWidth) {
        switch (valWidth) {
            case 1:
                return ZL_SENTINEL_DECODE_IMPL(1, 1);
            case 2:
                return ZL_SENTINEL_DECODE_IMPL(2, 2);
            case 4:
                return ZL_SENTINEL_DECODE_IMPL(4, 4);
            case 8:
                return ZL_SENTINEL_DECODE_IMPL(8, 8);
        }
        assert(false);
    }
    return ZL_sentinelDecode_generic(
            output,
            values,
            exceptions,
            numElts,
            numExceptions,
            valWidth,
            outWidth,
            sentinel);

#undef ZL_SENTINEL_DECODE_IMPL
}
