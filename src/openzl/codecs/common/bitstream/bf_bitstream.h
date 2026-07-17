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

/**
 * Initializes a backward-forward (BF) bit-writer over @p dst.
 *
 * Unlike the FF writer, a BF stream packs bits MSB-first and flushes whole
 * bytes *backward*, starting at the end of @p dst and moving toward its start.
 * This lays values out so a forward reader (see @ref ZS_BitDStreamBF_init)
 * recovers them in reverse of the write order -- the layout used by FSE/tANS
 * coders, which encode symbols in reverse so they decode in the original order.
 *
 * @param dst Destination buffer to write into.
 * @param dstCapacity Capacity of @p dst, in bytes.
 * @return An initialized writer positioned at the end of @p dst.
 */
ZL_INLINE ZS_BitCStreamBF
ZS_BitCStreamBF_init(uint8_t* dst, size_t dstCapacity);

/**
 * Flushes remaining bits, appends a padding marker, and finalizes the stream.
 *
 * A single set bit followed by zero-padding is written so the forward decoder
 * can locate the true start of the data (see ZS_BitDStreamBF_init()). Since the
 * stream grows backward, the finalized data occupies the tail of @p dst.
 *
 * @param bits The writer to finalize.
 * @return The number of bytes written (measured back from the end of @p dst),
 *         or an error if @p dst was too small.
 */
ZL_INLINE ZL_Report ZS_BitCStreamBF_finish(ZS_BitCStreamBF* bits);

/**
 * Appends the low @p nbBits bits of @p value to the stream (MSB-first).
 *
 * Bits are accumulated in the container until ZS_BitCStreamBF_flush() (or
 * ZS_BitCStreamBF_finish()) is called. The buffered bit count must not exceed
 * #ZS_BITSTREAM_WRITE_MAX_BITS between flushes.
 *
 * @param bits The writer.
 * @param value Source value; only its low @p nbBits bits are used.
 * @param nbBits Number of bits to write.
 */
ZL_INLINE void
ZS_BitCStreamBF_write(ZS_BitCStreamBF* bits, size_t value, size_t nbBits);

/**
 * Writes the whole bytes currently buffered in the container out to @p dst,
 * moving the write pointer backward.
 *
 * @param bits The writer.
 */
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

/**
 * Initializes a bit-reader for a backward-forward (BF) stream.
 *
 * A BF stream is decoded forward -- the reader wraps the FF decoder -- but the
 * data begins with a padding marker written by ZS_BitCStreamBF_finish(). This
 * constructor skips that leading padding (the zero bits up to and including the
 * first set bit) so reads begin at the first real value.
 *
 * @param src Source buffer; must point at the start of the BF data.
 * @param capacity Size of @p src, in bytes.
 * @return An initialized reader positioned past the padding marker.
 */
ZL_INLINE ZS_BitDStreamBF
ZS_BitDStreamBF_init(const uint8_t* src, size_t capacity);

/**
 * Reads @p nbBits bits and advances the stream past them.
 *
 * @param bits The reader.
 * @param nbBits Number of bits to read (at most #ZS_BITSTREAM_READ_MAX_BITS).
 * @return The decoded value in its low @p nbBits bits.
 */
ZL_INLINE size_t ZS_BitDStreamBF_read(ZS_BitDStreamBF* bits, size_t nbBits);

/**
 * Refills the container from the source to keep enough bits available between
 * reads.
 *
 * @param bits The reader.
 */
ZL_INLINE void ZS_BitDStreamBF_reload(ZS_BitDStreamBF* bits);

/**
 * Validates that the stream was consumed without over-reading and reports how
 * many bytes were consumed.
 *
 * @param bits The reader.
 * @return The number of bytes consumed, or an error on over-read.
 */
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
