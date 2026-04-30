// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZS2_BF_BITSTREAM_H
#define ZS2_BF_BITSTREAM_H

#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

#if ZL_HAS_BMI2
#    include <immintrin.h>
#endif

ZL_BEGIN_C_DECLS

typedef struct {
    size_t container;
    size_t nbBits;
    uint8_t* ptr;
    uint8_t* limit;
    uint8_t* end;
    uint8_t* begin;
} ZS_BitCStreamBF;

ZL_INLINE ZS_BitCStreamBF
ZS_BitCStreamBF_init(uint8_t* dst, size_t dstCapacity);
ZL_INLINE ZL_Report ZS_BitCStreamBF_finish(ZS_BitCStreamBF* bits);
ZL_INLINE void
ZS_BitCStreamBF_write(ZS_BitCStreamBF* bits, size_t value, size_t nbBits);
ZL_INLINE void ZS_BitCStreamBF_flush(ZS_BitCStreamBF* bits);

ZL_INLINE ZS_BitCStreamBF ZS_BitCStreamBF_init(uint8_t* dst, size_t dstCapacity)
{
    uint8_t* end   = dst + dstCapacity;
    uint8_t* limit = dst + sizeof(size_t);
    return (ZS_BitCStreamBF){ .container = 0,
                              .nbBits    = 0,
                              .ptr       = dst + dstCapacity,
                              .limit     = limit,
                              .end       = end,
                              .begin     = dst };
}

ZL_INLINE void
ZS_BitCStreamBF_write(ZS_BitCStreamBF* bits, size_t value, size_t nbBits)
{
    ZL_ASSERT_LE(bits->nbBits + nbBits, ZS_BITSTREAM_WRITE_MAX_BITS);
    size_t const mask = ((1ULL << nbBits) - 1);
    bits->container   = (bits->container << nbBits) | (value & mask);
    bits->nbBits += nbBits;
}

ZL_INLINE void ZS_BitCStreamBF_flush(ZS_BitCStreamBF* bits)
{
    ZL_ASSERT_LE(bits->nbBits, ZS_BITSTREAM_WRITE_MAX_BITS);
    size_t nbBytes                   = bits->nbBits / 8;
    const size_t kContainerNbBits    = sizeof(bits->container) * 8;
    const size_t kContainerShiftMask = kContainerNbBits - 1;
    if (ZL_LIKELY(bits->ptr > bits->limit)) {
        const size_t toWrite = bits->container
                << ((kContainerNbBits - bits->nbBits) & kContainerShiftMask);
        ZL_writeLE64(bits->ptr - sizeof(size_t), toWrite);
    } else {
        if (ZL_UNLIKELY(bits->begin > bits->ptr - nbBytes)) {
            // This is basically a failure condition, we will make a best effort
            // to write as much data as possible.
            nbBytes = (size_t)(bits->ptr - bits->begin);
        }
        size_t toWrite = bits->container
                >> ((bits->nbBits - nbBytes * 8) & kContainerShiftMask);
        ZL_writeLE64_N(bits->ptr - nbBytes, toWrite, nbBytes);
    }
    bits->nbBits -= (size_t)nbBytes * 8;
    bits->ptr -= nbBytes;
}

ZL_INLINE ZL_Report ZS_BitCStreamBF_finish(ZS_BitCStreamBF* bits)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT((ZL_OperationContext*)NULL);
    ZS_BitCStreamBF_flush(bits);
    size_t extraBits = 8 - (bits->nbBits % 8);
    ZS_BitCStreamBF_write(bits, 1 << (extraBits - 1), extraBits);
    ZL_ASSERT_EQ(bits->nbBits % 8, 0);
    ZS_BitCStreamBF_flush(bits);
    ZL_ERR_IF(bits->nbBits, dstCapacity_tooSmall);
    return ZL_returnValue((size_t)(bits->end - bits->ptr));
}

// We utilize the FF Bitstream decoder as they are both
// forward decoders with a slight difference in how
// the padding is handled (for BF we need to skip padding at the
// start).
typedef struct {
    ZS_BitDStreamFF bits;
} ZS_BitDStreamBF;

ZL_INLINE ZS_BitDStreamBF
ZS_BitDStreamBF_init(const uint8_t* src, size_t capacity);
ZL_INLINE size_t ZS_BitDStreamBF_read(ZS_BitDStreamBF* bits, size_t nbBits);
ZL_INLINE void ZS_BitDStreamBF_reload(ZS_BitDStreamBF* bits);
ZL_INLINE ZL_Report ZS_BitDStreamBF_finish(ZS_BitDStreamBF* bits);

ZL_INLINE ZS_BitDStreamBF
ZS_BitDStreamBF_init(const uint8_t* src, size_t capacity)
{
    ZS_BitDStreamFF bits     = ZS_BitDStreamFF_init(src, capacity);
    uint64_t const firstByte = ZS_BitDStreamFF_peek(&bits, 8);
    size_t const zeroBits    = (size_t)ZL_ctz64(firstByte);
    ZL_ASSERT_LT(zeroBits, 8);
    ZS_BitDStreamFF_skip(&bits, zeroBits + 1);
    ZS_BitDStreamFF_reload(&bits);
    return (ZS_BitDStreamBF){ .bits = bits };
}

ZL_INLINE size_t ZS_BitDStreamBF_read(ZS_BitDStreamBF* bits, size_t nbBits)
{
    return ZS_BitDStreamFF_read(&bits->bits, nbBits);
}

ZL_INLINE void ZS_BitDStreamBF_reload(ZS_BitDStreamBF* bits)
{
    ZS_BitDStreamFF_reload(&bits->bits);
}

ZL_INLINE ZL_Report ZS_BitDStreamBF_finish(ZS_BitDStreamBF* bits)
{
    return ZS_BitDStreamFF_finish(&bits->bits);
}

ZL_END_C_DECLS

#endif // ZS2_BF_BITSTREAM_H
