// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef ZSTRONG_COMMON_BITSTREAM_H
#define ZSTRONG_COMMON_BITSTREAM_H

#include "openzl/shared/bits.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

#if ZL_HAS_BMI2
#    include <immintrin.h>
#endif

ZL_BEGIN_C_DECLS

#define ZS_BITSTREAM_WRITE_MAX_BITS (sizeof(size_t) * 8 - 1)
#define ZS_BITSTREAM_READ_MAX_BITS (sizeof(size_t) * 8)

typedef struct {
    size_t container;
    size_t nbBits;
    uint8_t* ptr;
    uint8_t* limit;
    uint8_t* end;
    uint8_t* begin;
} ZS_BitCStreamFF;

ZL_INLINE ZS_BitCStreamFF
ZS_BitCStreamFF_init(uint8_t* dst, size_t dstCapacity);
ZL_INLINE ZL_Report ZS_BitCStreamFF_finish(ZS_BitCStreamFF* bits);
ZL_INLINE void
ZS_BitCStreamFF_write(ZS_BitCStreamFF* bits, size_t value, size_t nbBits);
ZL_INLINE void ZS_BitCStreamFF_flush(ZS_BitCStreamFF* bits);

ZL_INLINE void ZS_BitCStreamFF_writeExpGolomb(
        ZS_BitCStreamFF* bits,
        uint32_t value,
        size_t order)
{
    if (order > 0) {
        ZS_BitCStreamFF_write(bits, value, order);
        value = value >> order;
    }
    uint32_t const nbits = (uint32_t)ZL_highbit32(value + 1);
    ZS_BitCStreamFF_write(bits, (size_t)1 << nbits, nbits + 1);
    ZS_BitCStreamFF_write(bits, value + 1, nbits);
}

typedef struct {
    size_t container;
    size_t nbBitsRead;
    uint8_t const* ptr;
    uint8_t const* limit;
    uint8_t const* end;
} ZS_BitDStreamFF;

ZL_INLINE ZS_BitDStreamFF
ZS_BitDStreamFF_init(uint8_t const* src, size_t srcSize);
ZL_INLINE ZL_Report ZS_BitDStreamFF_finish(ZS_BitDStreamFF const* bits);
ZL_INLINE size_t ZS_BitDStreamFF_read(ZS_BitDStreamFF* bits, size_t nbBits);
ZL_INLINE size_t
ZS_BitDStreamFF_peek(ZS_BitDStreamFF const* bits, size_t nbBits);
ZL_INLINE void ZS_BitDStreamFF_skip(ZS_BitDStreamFF* bits, size_t nbBits);
ZL_INLINE void ZS_BitDStreamFF_reload(ZS_BitDStreamFF* bits);

ZL_INLINE uint32_t
ZS_BitDStreamFF_readExpGolomb(ZS_BitDStreamFF* bits, size_t order)
{
    uint32_t extra = 0;
    if (order > 0) {
        extra = (uint32_t)ZS_BitDStreamFF_read(bits, order);
    }
    uint32_t const nbits =
            (uint32_t)ZL_ctz32((uint32_t)ZS_BitDStreamFF_peek(bits, 32));
    ZS_BitDStreamFF_skip(bits, nbits + 1);
    uint32_t value =
            ((1u << nbits) | (uint32_t)ZS_BitDStreamFF_read(bits, nbits)) - 1;
    return (value << order) | extra;
}

ZL_INLINE ZS_BitCStreamFF ZS_BitCStreamFF_init(uint8_t* dst, size_t dstCapacity)
{
    return (ZS_BitCStreamFF){
        .container = 0,
        .nbBits    = 0,
        .ptr       = dst,
        .limit     = dst + dstCapacity - sizeof(size_t),
        .end       = dst + dstCapacity,
        .begin     = dst,
    };
}

ZL_INLINE ZL_Report ZS_BitCStreamFF_finish(ZS_BitCStreamFF* bits)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT((ZL_OperationContext*)NULL);
    size_t bytesToWrite = (bits->nbBits + 7) / 8;
    if (bits->end < bits->ptr + bytesToWrite)
        ZL_ERR(internalBuffer_tooSmall);
    if (bytesToWrite) {
        ZL_ASSERT_EQ(
                bits->container,
                bits->container & (((size_t)1 << bits->nbBits) - 1));
        ZL_writeLE64_N(bits->ptr, bits->container, bytesToWrite);
    }
    return ZL_returnValue((size_t)(bits->ptr - bits->begin) + bytesToWrite);
}

ZL_INLINE void
ZS_BitCStreamFF_write(ZS_BitCStreamFF* bits, size_t value, size_t nbBits)
{
    ZL_ASSERT_LE(bits->nbBits + nbBits, ZS_BITSTREAM_WRITE_MAX_BITS);
    size_t const mask = (((size_t)1 << nbBits) - 1);
    bits->container |= (value & mask) << bits->nbBits;
    bits->nbBits += nbBits;
}

ZL_INLINE void ZS_BitCStreamFF_flush(ZS_BitCStreamFF* bits)
{
    if (bits->ptr > bits->limit) {
        return;
    }
    size_t const nbBytes = bits->nbBits >> 3;
    ZL_writeLEST(bits->ptr, bits->container);
    bits->ptr += nbBytes;
    bits->nbBits &= 7;
    bits->container >>= (nbBytes << 3);
}

ZL_INLINE ZS_BitDStreamFF
ZS_BitDStreamFF_init(uint8_t const* src, size_t srcSize)
{
    if (ZL_LIKELY(srcSize >= sizeof(size_t))) {
        ZS_BitDStreamFF bits = {
            .container  = ZL_readLEST(src),
            .nbBitsRead = 0,
            .ptr        = src,
            .limit      = src + srcSize - sizeof(size_t) + 1,
            .end        = src + srcSize,
        };
        return bits;
    } else {
        ZS_BitDStreamFF bits = {
            .container  = 0,
            .nbBitsRead = (sizeof(size_t) - srcSize) * 8,
            .ptr        = src + srcSize,
            .limit      = src,
            .end        = src + srcSize,
        };
        for (size_t i = 0; i < srcSize; ++i) {
            bits.container |= (size_t)src[i] << (i << 3);
        }
        return bits;
    }
}

ZL_INLINE ZL_Report ZS_BitDStreamFF_finish(ZS_BitDStreamFF const* bits)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT((ZL_OperationContext*)NULL);
    if (bits->nbBitsRead > ZS_BITSTREAM_READ_MAX_BITS) {
        ZL_ERR(GENERIC);
    }
    return ZL_returnSuccess();
}

ZL_INLINE size_t ZS_BitDStreamFF_read(ZS_BitDStreamFF* bits, size_t nbBits)
{
    size_t const value = ZS_BitDStreamFF_peek(bits, nbBits);
    ZS_BitDStreamFF_skip(bits, nbBits);
    return value;
}

ZL_INLINE size_t
ZS_BitDStreamFF_peek(ZS_BitDStreamFF const* bits, size_t nbBits)
{
#if ZL_HAS_BMI2
    return _bzhi_u64(bits->container, nbBits);
#else
    // return _bzhi_u64(bits->container, nbBits);
    // return bits->container << (64 - nbBits) >> (64 - nbBits);
    return bits->container & (((size_t)1 << (nbBits & 63)) - 1);
#endif
}

ZL_INLINE void ZS_BitDStreamFF_skip(ZS_BitDStreamFF* bits, size_t nbBits)
{
    bits->container >>= nbBits;
    bits->nbBitsRead += nbBits;
}

ZL_INLINE void ZS_BitDStreamFF_reload(ZS_BitDStreamFF* bits)
{
    bits->ptr += bits->nbBitsRead >> 3;
    if (ZL_LIKELY(bits->ptr < bits->limit)) {
        size_t const next = ZL_readLEST(bits->ptr);
        bits->nbBitsRead &= 7;
        bits->container = next >> bits->nbBitsRead;
        return;
    }

    if (bits->ptr >= bits->end)
        return;

    uint8_t const* const limit = bits->limit - 1;
    size_t const skippedBits   = (size_t)((bits->ptr - limit) << 3);
    size_t const next          = ZL_readLEST(limit);
    bits->nbBitsRead &= 7;
    bits->container = next >> (bits->nbBitsRead + skippedBits);
}

ZL_END_C_DECLS

#endif // ZSTRONG_COMMON_BITSTREAM_H
