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

/**
 * Initializes a forward-forward (FF) bit-writer over @p dst.
 *
 * An FF stream packs bits LSB-first into a word-sized container and flushes
 * whole bytes forward, from @p dst toward higher addresses. Values are read
 * back in the same order they were written (see @ref ZS_BitDStreamFF_init).
 *
 * @param dst Destination buffer to write into.
 * @param dstCapacity Capacity of @p dst, in bytes.
 * @return An initialized writer positioned at the start of @p dst.
 */
ZL_INLINE ZS_BitCStreamFF
ZS_BitCStreamFF_init(uint8_t* dst, size_t dstCapacity);

/**
 * Flushes any remaining buffered bits and finalizes the stream.
 *
 * @param bits The writer to finalize.
 * @return The total number of bytes written, or an error if @p dst was too
 *         small to hold the trailing bits.
 */
ZL_INLINE ZL_Report ZS_BitCStreamFF_finish(ZS_BitCStreamFF* bits);

/**
 * Appends the low @p nbBits bits of @p value to the stream.
 *
 * Bits are accumulated in the container and are not committed to @p dst until
 * ZS_BitCStreamFF_flush() (or ZS_BitCStreamFF_finish()) is called. The caller
 * must flush often enough that the buffered bit count never exceeds
 * #ZS_BITSTREAM_WRITE_MAX_BITS between flushes.
 *
 * @param bits The writer.
 * @param value Source value; only its low @p nbBits bits are used.
 * @param nbBits Number of bits to write.
 */
ZL_INLINE void
ZS_BitCStreamFF_write(ZS_BitCStreamFF* bits, size_t value, size_t nbBits);

/**
 * Writes the whole bytes currently buffered in the container out to @p dst,
 * retaining the leftover (< 8) bits for subsequent writes.
 *
 * Becomes a no-op once the write pointer reaches the buffer's safety limit, so
 * ZS_BitCStreamFF_finish() must still be called to emit the final partial word.
 *
 * @param bits The writer.
 */
ZL_INLINE void ZS_BitCStreamFF_flush(ZS_BitCStreamFF* bits);

/**
 * Reserves a byte-aligned region of @p nbBits bits for raw, direct writes.
 *
 * Flushes buffered bits up to the next byte boundary, then returns a pointer to
 * a contiguous region of ceil(@p nbBits / 8) bytes that the caller may fill
 * directly (e.g. to embed a raw bitmap). After filling it, call
 * ZS_BitCStreamFF_commitReservedBits() before resuming bit-level writes.
 *
 * @param bits The writer.
 * @param nbBits Size of the region to reserve, in bits.
 * @return A pointer to the reserved region, or NULL if @p dst cannot hold it.
 */
ZL_INLINE uint8_t* ZS_BitCStreamFF_reserveAlignedBits(
        ZS_BitCStreamFF* bits,
        size_t nbBits);

/**
 * Resumes bit-level writing after a ZS_BitCStreamFF_reserveAlignedBits()
 * region.
 *
 * Reloads the partial trailing byte of the reserved region back into the
 * container so the residual (nbBits & 7) bits are carried by subsequent writes.
 *
 * @param bits The writer.
 */
ZL_INLINE void ZS_BitCStreamFF_commitReservedBits(ZS_BitCStreamFF* bits);

/**
 * Writes @p value as an order-@p order exponential-Golomb code.
 *
 * The low @p order bits of @p value are emitted verbatim, then the remaining
 * high bits are coded with an Elias gamma code (a unary length prefix followed
 * by the mantissa). Smaller values cost fewer bits, so this is suited to
 * distributions skewed toward zero. Decode with ZS_BitDStreamFF_readExpGolomb()
 * using the same @p order.
 *
 * @param bits The writer.
 * @param value Value to encode.
 * @param order Number of low bits stored verbatim (0 for plain exp-Golomb).
 */
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
    uint8_t const* begin;
} ZS_BitDStreamFF;

/**
 * Initializes a forward-forward (FF) bit-reader over @p src.
 *
 * Reads back bits in the order they were written by an FF bit-writer. Streams
 * shorter than a machine word are handled specially so that reads stay in
 * bounds.
 *
 * @param src Source buffer to read from.
 * @param srcSize Size of @p src, in bytes.
 * @return An initialized reader positioned at the start of @p src.
 */
ZL_INLINE ZS_BitDStreamFF
ZS_BitDStreamFF_init(uint8_t const* src, size_t srcSize);

/**
 * Validates that the stream was consumed without over-reading and reports how
 * many bytes were consumed.
 *
 * @param bits The reader.
 * @return The number of bytes consumed, or an error if more bits were read than
 *         the stream contained.
 */
ZL_INLINE ZL_Report ZS_BitDStreamFF_finish(ZS_BitDStreamFF const* bits);

/**
 * Reads @p nbBits bits and advances the stream past them.
 *
 * Equivalent to ZS_BitDStreamFF_peek() followed by ZS_BitDStreamFF_skip(). The
 * container must hold at least @p nbBits unconsumed bits; call
 * ZS_BitDStreamFF_reload() periodically to refill it.
 *
 * @param bits The reader.
 * @param nbBits Number of bits to read (at most #ZS_BITSTREAM_READ_MAX_BITS).
 * @return The decoded value in its low @p nbBits bits.
 */
ZL_INLINE size_t ZS_BitDStreamFF_read(ZS_BitDStreamFF* bits, size_t nbBits);

/**
 * Returns the next @p nbBits bits without consuming them.
 *
 * @param bits The reader.
 * @param nbBits Number of bits to inspect.
 * @return The peeked value in its low @p nbBits bits.
 */
ZL_INLINE size_t
ZS_BitDStreamFF_peek(ZS_BitDStreamFF const* bits, size_t nbBits);

/**
 * Advances the stream by @p nbBits bits without returning them.
 *
 * @param bits The reader.
 * @param nbBits Number of bits to skip.
 */
ZL_INLINE void ZS_BitDStreamFF_skip(ZS_BitDStreamFF* bits, size_t nbBits);

/**
 * Refills the container from @p src, discarding whole bytes already consumed.
 *
 * Should be called periodically between reads to keep enough bits available.
 * Once the stream is exhausted it leaves the consumed-bit count intact so that
 * ZS_BitDStreamFF_finish() can still detect an over-read.
 *
 * @param bits The reader.
 */
ZL_INLINE void ZS_BitDStreamFF_reload(ZS_BitDStreamFF* bits);

/**
 * Consumes a byte-aligned region of @p nbBits bits and returns a raw pointer to
 * it.
 *
 * Byte-aligns the current read position (regardless of any prior reloads or
 * over-reads), returns a pointer to the ceil(@p nbBits / 8)-byte region, and
 * repositions the reader just past it. This is the decode-side counterpart to
 * ZS_BitCStreamFF_reserveAlignedBits().
 *
 * @param bits The reader.
 * @param nbBits Size of the region to pop, in bits.
 * @return A pointer to the aligned region, or NULL if it lies out of bounds.
 */
ZL_INLINE uint8_t const* ZS_BitDStreamFF_popAlignedBits(
        ZS_BitDStreamFF* bits,
        size_t nbBits);

/**
 * Reads a value encoded by ZS_BitCStreamFF_writeExpGolomb().
 *
 * @param bits The reader.
 * @param order Order used at encode time; must match the encoder's @p order.
 * @return The decoded value.
 */
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
    const size_t limit =
            dstCapacity < sizeof(size_t) ? 0 : dstCapacity - sizeof(size_t) + 1;
    return (ZS_BitCStreamFF){
        .container = 0,
        .nbBits    = 0,
        .ptr       = dst,
        .limit     = dst + limit,
        .end       = dst + dstCapacity,
        .begin     = dst,
    };
}

ZL_INLINE ZL_Report ZS_BitCStreamFF_finish(ZS_BitCStreamFF* bits)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT((ZL_OperationContext*)NULL);
    size_t bytesToWrite = (bits->nbBits + 7) / 8;
    if ((size_t)(bits->end - bits->ptr) < bytesToWrite)
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
    if (bits->ptr >= bits->limit) {
        return;
    }
    size_t const nbBytes = bits->nbBits >> 3;
    ZL_writeLEST(bits->ptr, bits->container);
    bits->ptr += nbBytes;
    bits->nbBits &= 7;
    bits->container >>= (nbBytes << 3);
}

ZL_INLINE uint8_t* ZS_BitCStreamFF_reserveAlignedBits(
        ZS_BitCStreamFF* bits,
        size_t nbBits)
{
    if (nbBits > SIZE_MAX - 7) {
        return NULL;
    }

    size_t const bufferedBytes = (bits->nbBits + 7) / 8;
    if (bufferedBytes > (size_t)(bits->end - bits->ptr)) {
        return NULL;
    }

    size_t const nbBytes   = (nbBits + 7) / 8;
    size_t const available = (size_t)(bits->end - bits->ptr) - bufferedBytes;
    if (nbBytes > available) {
        return NULL;
    }

    ZL_ASSERT_LE(bufferedBytes, sizeof(size_t));
    if (bufferedBytes != 0) {
        ZL_writeLE64_N(bits->ptr, bits->container, bufferedBytes);
        bits->ptr += bufferedBytes;
        bits->container = 0;
        bits->nbBits    = 0;
    }
    ZL_ASSERT_EQ(bits->nbBits, 0);

    uint8_t* const out = bits->ptr;
    bits->ptr          = out + (nbBits / 8);
    bits->nbBits       = nbBits & 7;
    bits->container    = 0;
    return out;
}

ZL_INLINE void ZS_BitCStreamFF_commitReservedBits(ZS_BitCStreamFF* bits)
{
    if (bits->nbBits == 0) {
        bits->container = 0;
        return;
    }
    bits->container = bits->ptr[0] & (((size_t)1 << bits->nbBits) - 1);
}

/**
 * Little-endian load of the first @p nbBytes bytes at @p src into a container
 * word.
 *
 * Used for streams shorter than a full word, where every byte lives in the
 * container and a full-width load would read out of bounds.
 *
 * @param src Source bytes.
 * @param nbBytes Number of bytes to load; must be < sizeof(size_t).
 * @return The loaded bytes packed little-endian into the low bits of the word.
 */
ZL_INLINE size_t ZS_BitDStreamFF_loadPartial(uint8_t const* src, size_t nbBytes)
{
    size_t container = 0;
    for (size_t i = 0; i < nbBytes; ++i) {
        container |= (size_t)src[i] << (i << 3);
    }
    return container;
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
            .begin      = src,
        };
        return bits;
    } else {
        uint8_t const* const end = srcSize > 0 ? src + srcSize : src;
        ZS_BitDStreamFF bits     = {
                .container  = ZS_BitDStreamFF_loadPartial(src, srcSize),
                .nbBitsRead = (sizeof(size_t) - srcSize) * 8,
                .ptr        = end,
                .limit      = src,
                .end        = end,
                .begin      = src,
        };
        return bits;
    }
}

ZL_INLINE ZL_Report ZS_BitDStreamFF_finish(ZS_BitDStreamFF const* bits)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT((ZL_OperationContext*)NULL);
    if (bits->nbBitsRead > ZS_BITSTREAM_READ_MAX_BITS) {
        ZL_ERR(GENERIC);
    }
    size_t bytesRead = (size_t)(bits->ptr - bits->begin);
    bytesRead += bits->nbBitsRead >> 3;
    bytesRead += (bits->nbBitsRead & 7) != 0;
    if ((size_t)(bits->end - bits->begin) < sizeof(size_t)) {
        bytesRead -= sizeof(size_t);
    }
    return ZL_returnValue(bytesRead);
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
    uint8_t const* const ptr = bits->ptr + (bits->nbBitsRead >> 3);
    if (ZL_LIKELY(ptr < bits->limit)) {
        bits->ptr = ptr;
        bits->nbBitsRead &= 7;
        bits->container = ZL_readLEST(ptr) >> bits->nbBitsRead;
        return;
    }

    // Past the end: leave ptr and nbBitsRead untouched. This keeps the consumed
    // bit count -- (ptr - begin) * 8 + nbBitsRead -- exact for finish() and
    // popAlignedBits(), while an over-read still leaves nbBitsRead too large
    // for finish() to accept.
    if (ptr >= bits->end)
        return;

    uint8_t const* const tail = bits->limit - 1;
    size_t const skippedBits  = (size_t)((ptr - tail) << 3);
    bits->ptr                 = ptr;
    bits->nbBitsRead &= 7;
    bits->container = ZL_readLEST(tail) >> (bits->nbBitsRead + skippedBits);
}

ZL_INLINE uint8_t const* ZS_BitDStreamFF_popAlignedBits(
        ZS_BitDStreamFF* bits,
        size_t nbBits)
{
    if (nbBits > SIZE_MAX - 7) {
        return NULL;
    }

    // check if the stream is already in an invalid state
    if (bits->nbBitsRead > ZS_BITSTREAM_READ_MAX_BITS) {
        return NULL;
    }

    size_t const streamSize = (size_t)(bits->end - bits->begin);
    if (streamSize > SIZE_MAX / 8) {
        return NULL;
    }

    size_t consumedBits =
            (size_t)(bits->ptr - bits->begin) * 8 + bits->nbBitsRead;
    if (streamSize < sizeof(size_t)) {
        consumedBits -= sizeof(size_t) * 8;
    }

    size_t const bitSize = streamSize * 8;
    if (consumedBits > bitSize || consumedBits > SIZE_MAX - 7) {
        return NULL;
    }

    size_t const alignedBits = ((consumedBits + 7) / 8) * 8;
    if (nbBits > bitSize - alignedBits) {
        return NULL;
    }

    uint8_t const* const out = bits->begin + alignedBits / 8;

    // Reposition the stream just past the aligned region we handed out.
    size_t const bitPos      = alignedBits + nbBits;
    uint8_t const* const pos = bits->begin + bitPos / 8;
    size_t const bitShift    = bitPos & 7;

    if (streamSize >= sizeof(size_t)) {
        // Refill the 64-bit window at `pos`, clamping the load back from the
        // end when fewer than 8 bytes remain; the matching shift exposes the
        // same bit either way. A shift of the full word width (the whole
        // stream consumed) leaves an empty window.
        uint8_t const* const tail = bits->end - sizeof(size_t);
        uint8_t const* const load = pos <= tail ? pos : tail;
        size_t const shift        = bitPos - (size_t)(load - bits->begin) * 8;
        bits->container =
                shift < sizeof(size_t) * 8 ? ZL_readLEST(load) >> shift : 0;
        bits->nbBitsRead = bitShift;
        bits->ptr        = pos;
    } else {
        // Short stream: every byte already lives in the container.
        size_t const remaining = (size_t)(bits->end - pos);
        bits->container        = ZS_BitDStreamFF_loadPartial(pos, remaining);
        bits->container >>= bitShift;
        bits->nbBitsRead = (sizeof(size_t) - remaining) * 8 + bitShift;
        bits->ptr        = bits->end;
    }
    return out;
}

ZL_END_C_DECLS

#endif // ZSTRONG_COMMON_BITSTREAM_H
