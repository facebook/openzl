// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * \file
 *
 * This file defines two fundamental abstractions for linearly operating over
 * buffers, the ZL_ReadCursor and ZL_WriteCursor.
 */

#ifndef ZSTRONG_COMMON_CURSOR_H
#define ZSTRONG_COMMON_CURSOR_H

#include <stdbool.h> // bool
#include <stddef.h>  // size_t
#include <stdint.h>  // uint8_t
#include <string.h>  // memcpy

#include "openzl/common/debug.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/varint.h"

ZL_BEGIN_C_DECLS

/**
 * The ZL_ReadCursor abstraction is a non-owning reference to a buffer. It is
 * designed for linear consumption of the contents of the buffer, and keeps
 * track of the current position and remaining space.
 *
 * It's recommended that you only interact with this struct via the functions
 * below.
 */
typedef struct {
    const uint8_t* _cur;
    const uint8_t* _end;
} ZL_ReadCursor;
#define ZL_RC ZL_ReadCursor /// Define a convenient short alias.

/**
 * The ZL_WriteCursor abstraction is a non-owning reference to a buffer.
 * It is designed for linear production of the contents of the buffer,
 * and keeps track of the current position and used and remaining space.
 *
 * The cursor _doesn't guarantee_ that it will not write out of bound.
 * It only asserts this condition, which is useful in debug mode only.
 * In production mode, this guarantee must be provided via other means.
 *
 * It's recommended that you only interact with this struct
 * via the functions below.
 */
typedef struct {
    uint8_t* _begin;
    uint8_t* _cur;
    uint8_t* _end;
} ZL_WriteCursor;
#define ZL_WC ZL_WriteCursor /// Define a convenient short alias.

// ZL_ReadCursor Debug Functions //

ZL_INLINE bool ZL_RC_valid(const ZL_RC* rc)
{
    return rc && rc->_cur <= rc->_end;
}

ZL_INLINE void ZL_RC_validate(const ZL_RC* rc)
{
    ZL_ASSERT(ZL_RC_valid(rc));
}

ZL_INLINE void ZL_RC_log(const ZL_RC* rc)
{
    ZL_LOG(DEBUG,
           "ZL_ReadCursor @ %p: cur %p end %p avail %ld",
           (const void*)rc,
           (const void*)rc->_cur,
           (const void*)rc->_end,
           rc->_end - rc->_cur);
}

// ZL_WriteCursor Debug Functions //

ZL_INLINE bool ZL_WC_valid(const ZL_WC* wc)
{
    return wc && wc->_begin <= wc->_cur && wc->_cur <= wc->_end;
}

ZL_INLINE void ZL_WC_validate(const ZL_WC* wc)
{
    ZL_ASSERT(ZL_WC_valid(wc));
}

ZL_INLINE void ZL_WC_log(const ZL_WC* wc)
{
    ZL_LOG(DEBUG,
           "ZL_WriteCursor @ %p: begin %p cur %p end %p size %ld avail %ld cap %ld",
           (const void*)wc,
           (const void*)wc->_begin,
           (const void*)wc->_cur,
           (const void*)wc->_end,
           wc->_cur - wc->_begin,
           wc->_end - wc->_cur,
           wc->_end - wc->_begin);
}

// ZL_ReadCursor Constructor //

/**
 * The buffer is assumed to be full of content. (Since the cursor operates over
 * an immutable view of a buffer, there's no point in being aware of unused
 * capacity.)
 */
ZL_INLINE ZL_RC ZL_RC_wrap(const uint8_t* buf, size_t size)
{
    ZL_RC rc = { ._cur = buf, ._end = buf + size };
    ZL_RC_validate(&rc);
    return rc;
}

ZL_INLINE ZL_RC ZL_RC_makeEmpty(void)
{
    return ZL_RC_wrap(NULL, 0);
}

/**
 * Produce a ReadCursor that represents the content that's been written to the
 * provided WriteCursor.
 */
ZL_INLINE ZL_RC ZL_RC_wrapWC(const ZL_WC* wc)
{
    ZL_WC_validate(wc);
    ZL_RC rc = ZL_RC_wrap(wc->_begin, (size_t)(wc->_cur - wc->_begin));
    return rc;
}

// ZL_ReadCursor Methods //

ZL_INLINE const uint8_t* ZL_RC_ptr(const ZL_RC* rc)
{
    ZL_RC_validate(rc);
    return rc->_cur;
}

ZL_INLINE const uint8_t* ZL_RC_end(const ZL_RC* rc)
{
    ZL_RC_validate(rc);
    return rc->_end;
}

ZL_INLINE size_t ZL_RC_avail(const ZL_RC* rc)
{
    ZL_RC_validate(rc);
    return (size_t)(rc->_end - rc->_cur);
}

ZL_INLINE bool ZL_RC_has(const ZL_RC* rc, size_t needed)
{
    return ZL_RC_avail(rc) >= needed;
}

#define ZL_RC_ASSERT_HAS(rc, needed) ZL_ASSERT_GE(ZL_RC_avail(rc), needed)
#define ZL_RC_REQUIRE_HAS(rc, needed) ZL_REQUIRE_GE(ZL_RC_avail(rc), needed)

/**
 * Creates a new read cursor that references the first `size`
 * bytes of the `rc` from [ZSTD_RC_ptr(rc), ZSTD_RC_ptr(rc) + size).
 * The original `rc` is not modified.
 */
ZL_INLINE ZL_RC ZL_RC_prefix(const ZL_RC* rc, size_t size)
{
    ZL_RC_validate(rc);
    ZL_RC_ASSERT_HAS(rc, size);
    return ZL_RC_wrap(ZL_RC_ptr(rc), size);
}

ZL_INLINE void ZL_RC_advance(ZL_RC* rc, size_t size)
{
    ZL_RC_validate(rc);
    ZL_RC_ASSERT_HAS(rc, size);
    rc->_cur += size;
    ZL_RC_validate(rc);
}

ZL_INLINE uint8_t ZL_RC_pop(ZL_RC* rc)
{
    ZL_RC_validate(rc);
    ZL_RC_ASSERT_HAS(rc, 1);
    uint8_t val = *(rc->_cur++);
    ZL_RC_validate(rc);
    return val;
}

ZL_INLINE const uint8_t* ZL_RC_pull(ZL_RC* rc, size_t size)
{
    const uint8_t* ptr = ZL_RC_ptr(rc);
    ZL_RC_advance(rc, size);
    return ptr;
}

/// Remove `size` bytes from the end of the read cursor.
ZL_INLINE void ZL_RC_subtract(ZL_RC* rc, size_t size)
{
    ZL_RC_validate(rc);
    ZL_RC_ASSERT_HAS(rc, size);
    rc->_end -= size;
    ZL_RC_validate(rc);
}

/// Pop the last byte from the read cursor.
ZL_INLINE uint8_t ZL_RC_rPop(ZL_RC* rc)
{
    ZL_RC_subtract(rc, 1);
    return *rc->_end;
}

/**
 * Consume the last `size` bytes from the end of the read cursor.
 * @returns a pointer to the last `size` bytes of the `rc`.
 */
ZL_INLINE const uint8_t* ZL_RC_rPull(ZL_RC* rc, size_t size)
{
    ZL_RC_subtract(rc, size);
    return rc->_end;
}

// 2 byte reads

ZL_INLINE uint16_t ZL_RC_popCE16(ZL_RC* rc)
{
    return ZL_readCE16(ZL_RC_pull(rc, 2));
}

ZL_INLINE uint16_t ZL_RC_popHE16(ZL_RC* rc)
{
    return ZL_read16(ZL_RC_pull(rc, 2));
}

ZL_INLINE uint16_t ZL_RC_popBE16(ZL_RC* rc)
{
    return ZL_readBE16(ZL_RC_pull(rc, 2));
}

ZL_INLINE uint16_t ZL_RC_popLE16(ZL_RC* rc)
{
    return ZL_readLE16(ZL_RC_pull(rc, 2));
}

// 3 byte reads

ZL_INLINE uint32_t ZL_RC_popCE24(ZL_RC* rc)
{
    return ZL_readCE24(ZL_RC_pull(rc, 3));
}

ZL_INLINE uint32_t ZL_RC_popHE24(ZL_RC* rc)
{
    return ZL_read24(ZL_RC_pull(rc, 3));
}

ZL_INLINE uint32_t ZL_RC_popBE24(ZL_RC* rc)
{
    return ZL_readBE24(ZL_RC_pull(rc, 3));
}

ZL_INLINE uint32_t ZL_RC_popLE24(ZL_RC* rc)
{
    return ZL_readLE24(ZL_RC_pull(rc, 3));
}

// 4 byte reads

ZL_INLINE uint32_t ZL_RC_popCE32(ZL_RC* rc)
{
    return ZL_readCE32(ZL_RC_pull(rc, 4));
}

ZL_INLINE uint32_t ZL_RC_popHE32(ZL_RC* rc)
{
    return ZL_read32(ZL_RC_pull(rc, 4));
}

ZL_INLINE uint32_t ZL_RC_popBE32(ZL_RC* rc)
{
    return ZL_readBE32(ZL_RC_pull(rc, 4));
}

ZL_INLINE uint32_t ZL_RC_popLE32(ZL_RC* rc)
{
    return ZL_readLE32(ZL_RC_pull(rc, 4));
}

/// Pop a 32-bit host-endian integer from the end of the RC
ZL_INLINE uint32_t ZL_RC_rPopHE32(ZL_RC* rc)
{
    return ZL_read32(ZL_RC_rPull(rc, 4));
}

/// Pop a 32-bit big-endian integer from the end of the RC
ZL_INLINE uint32_t ZL_RC_rPopBE32(ZL_RC* rc)
{
    return ZL_readBE32(ZL_RC_rPull(rc, 4));
}

/// Pop a 32-bit little-endian integer from the end of the RC
ZL_INLINE uint32_t ZL_RC_rPopLE32(ZL_RC* rc)
{
    return ZL_readLE32(ZL_RC_rPull(rc, 4));
}

// 8 byte reads

ZL_INLINE uint64_t ZL_RC_popCE64(ZL_RC* rc)
{
    return ZL_readLE64(ZL_RC_pull(rc, 8));
}

ZL_INLINE uint64_t ZL_RC_popHE64(ZL_RC* rc)
{
    return ZL_read64(ZL_RC_pull(rc, 8));
}

ZL_INLINE uint64_t ZL_RC_popBE64(ZL_RC* rc)
{
    return ZL_readBE64(ZL_RC_pull(rc, 8));
}

ZL_INLINE uint64_t ZL_RC_popLE64(ZL_RC* rc)
{
    return ZL_readLE64(ZL_RC_pull(rc, 8));
}

// size_t reads

ZL_INLINE size_t ZL_RC_popCEST(ZL_RC* rc)
{
    return ZL_readCEST(ZL_RC_pull(rc, sizeof(size_t)));
}

ZL_INLINE size_t ZL_RC_popHEST(ZL_RC* rc)
{
    return ZL_readST(ZL_RC_pull(rc, sizeof(size_t)));
}

ZL_INLINE size_t ZL_RC_popBEST(ZL_RC* rc)
{
    return ZL_readBEST(ZL_RC_pull(rc, sizeof(size_t)));
}

ZL_INLINE size_t ZL_RC_popLEST(ZL_RC* rc)
{
    return ZL_readLEST(ZL_RC_pull(rc, sizeof(size_t)));
}

// varint reads

ZL_INLINE ZL_RESULT_OF(uint64_t) ZL_RC_popVarint(ZL_RC* rc)
{
    return ZL_varintDecode(&rc->_cur, rc->_end);
}

ZL_INLINE ZL_Report ZL_RC_popVarint32(ZL_RC* rc, uint32_t* valPtr)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT((ZL_OperationContext*)NULL);
    ZL_RESULT_OF(uint64_t) res = ZL_varintDecode(&rc->_cur, rc->_end);
    if (ZL_RES_isError(res)) {
        ZL_ERR(GENERIC);
    }
    uint64_t tmp = ZL_RES_value(res);
    if (tmp > UINT32_MAX) {
        ZL_ERR(GENERIC);
    }
    *valPtr = (uint32_t)tmp;
    return ZL_returnSuccess();
}

// Read variable size integer
ZL_INLINE uint64_t ZL_RC_popHE(ZL_RC* rc, size_t size)
{
    switch (size) {
        case 1:
            return (uint64_t)ZL_RC_pop(rc);
        case 2:
            return (uint64_t)ZL_RC_popHE16(rc);
        case 4:
            return (uint64_t)ZL_RC_popHE32(rc);
        case 8:
            return (uint64_t)ZL_RC_popHE64(rc);
        default:
            ZL_ASSERT_FAIL("Unsupported size %u", (unsigned)size);
            return 0;
    }
}

// ZL_WriteCursor Constructor //

ZL_INLINE ZL_WC ZL_WC_wrapPartial(uint8_t* buf, size_t used, size_t capacity)
{
    ZL_WC wc = { ._begin = buf, ._cur = buf + used, ._end = buf + capacity };
    ZL_WC_validate(&wc);
    return wc;
}

/**
 * The buffer is assumed to be empty.
 */
ZL_INLINE ZL_WC ZL_WC_wrap(uint8_t* buf, size_t size)
{
    return ZL_WC_wrapPartial(buf, 0, size);
}

ZL_INLINE ZL_WC ZL_WC_wrapFull(uint8_t* buf, size_t size)
{
    return ZL_WC_wrapPartial(buf, size, size);
}

ZL_INLINE ZL_WC ZL_WC_makeEmpty(void)
{
    return ZL_WC_wrap(NULL, 0);
}

ZL_INLINE void ZL_WC_reset(ZL_WC* wc)
{
    ZL_WC_validate(wc);
    wc->_cur = wc->_begin;
}

// ZL_WriteCursor Methods //

ZL_INLINE uint8_t* ZL_WC_ptr(ZL_WC* wc)
{
    ZL_WC_validate(wc);
    return wc->_cur;
}

ZL_INLINE const uint8_t* ZL_WC_cptr(const ZL_WC* wc)
{
    ZL_WC_validate(wc);
    return wc->_cur;
}

ZL_INLINE uint8_t* ZL_WC_begin(ZL_WC* wc)
{
    ZL_WC_validate(wc);
    return wc->_begin;
}

ZL_INLINE const uint8_t* ZL_WC_cbegin(const ZL_WC* wc)
{
    ZL_WC_validate(wc);
    return wc->_begin;
}

/**
 * Returns the number of bytes written so far.
 */
ZL_INLINE size_t ZL_WC_size(const ZL_WC* wc)
{
    ZL_WC_validate(wc);
    return (size_t)(wc->_cur - wc->_begin);
}

/**
 * Returns the remaining capacity (how many more bytes could be written).
 */
ZL_INLINE size_t ZL_WC_avail(const ZL_WC* wc)
{
    ZL_WC_validate(wc);
    return (size_t)(wc->_end - wc->_cur);
}

/**
 * Returns the total capacity (used + unused aka size() + avail()).
 */
ZL_INLINE size_t ZL_WC_capacity(const ZL_WC* wc)
{
    ZL_WC_validate(wc);
    return (size_t)(wc->_end - wc->_begin);
}

ZL_INLINE bool ZL_WC_has(const ZL_WC* wc, size_t needed)
{
    return ZL_WC_avail(wc) >= needed;
}

#define ZL_WC_ASSERT_HAS(wc, needed) ZL_ASSERT_GE(ZL_WC_avail(wc), needed)
#define ZL_WC_REQUIRE_HAS(wc, needed) ZL_REQUIRE_GE(ZL_WC_avail(wc), needed)

ZL_INLINE void ZL_WC_advance(ZL_WC* wc, size_t size)
{
    ZL_WC_validate(wc);
    ZL_WC_ASSERT_HAS(wc, size);
    wc->_cur += size;
    ZL_WC_validate(wc);
}

ZL_INLINE void ZL_WC_push(ZL_WC* wc, uint8_t val)
{
    ZL_WC_validate(wc);
    ZL_WC_ASSERT_HAS(wc, 1);
    *(wc->_cur++) = val;
    ZL_WC_validate(wc);
}

ZL_INLINE void ZL_WC_shove(ZL_WC* wc, const uint8_t* vals, size_t size)
{
    ZL_WC_validate(wc);
    ZL_WC_ASSERT_HAS(wc, size);
    memcpy(wc->_cur, vals, size);
    ZL_WC_advance(wc, size);
}

// 2 byte writes

ZL_INLINE void ZL_WC_pushCE16(ZL_WC* wc, uint16_t val)
{
    ZL_WC_ASSERT_HAS(wc, 2);
    ZL_writeCE16(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, 2);
}

ZL_INLINE void ZL_WC_pushHE16(ZL_WC* wc, uint16_t val)
{
    ZL_WC_ASSERT_HAS(wc, 2);
    ZL_write16(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, 2);
}

ZL_INLINE void ZL_WC_pushBE16(ZL_WC* wc, uint16_t val)
{
    ZL_WC_ASSERT_HAS(wc, 2);
    ZL_writeBE16(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, 2);
}

ZL_INLINE void ZL_WC_pushLE16(ZL_WC* wc, uint16_t val)
{
    ZL_WC_ASSERT_HAS(wc, 2);
    ZL_writeLE16(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, 2);
}

// 3 byte writes

ZL_INLINE void ZL_WC_pushCE24(ZL_WC* wc, uint32_t val)
{
    ZL_ASSERT_LT(val, 1 << 24);
    ZL_WC_ASSERT_HAS(wc, 3);
    ZL_writeCE24(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, 3);
}

ZL_INLINE void ZL_WC_pushHE24(ZL_WC* wc, uint32_t val)
{
    ZL_ASSERT_LT(val, 1 << 24);
    ZL_WC_ASSERT_HAS(wc, 3);
    ZL_write24(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, 3);
}

ZL_INLINE void ZL_WC_pushBE24(ZL_WC* wc, uint32_t val)
{
    ZL_ASSERT_LT(val, 1 << 24);
    ZL_WC_ASSERT_HAS(wc, 3);
    ZL_writeBE24(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, 3);
}

ZL_INLINE void ZL_WC_pushLE24(ZL_WC* wc, uint32_t val)
{
    ZL_ASSERT_LT(val, 1 << 24);
    ZL_WC_ASSERT_HAS(wc, 3);
    ZL_writeLE24(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, 3);
}

// 4 byte writes

ZL_INLINE void ZL_WC_pushCE32(ZL_WC* wc, uint32_t val)
{
    ZL_WC_ASSERT_HAS(wc, 4);
    ZL_writeCE32(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, 4);
}

ZL_INLINE void ZL_WC_pushHE32(ZL_WC* wc, uint32_t val)
{
    ZL_WC_ASSERT_HAS(wc, 4);
    ZL_write32(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, 4);
}

ZL_INLINE void ZL_WC_pushBE32(ZL_WC* wc, uint32_t val)
{
    ZL_WC_ASSERT_HAS(wc, 4);
    ZL_writeBE32(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, 4);
}

ZL_INLINE void ZL_WC_pushLE32(ZL_WC* wc, uint32_t val)
{
    ZL_WC_ASSERT_HAS(wc, 4);
    ZL_writeLE32(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, 4);
}

// 8 byte writes

ZL_INLINE void ZL_WC_pushCE64(ZL_WC* wc, uint64_t val)
{
    ZL_WC_ASSERT_HAS(wc, 8);
    ZL_writeCE64(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, 8);
}

ZL_INLINE void ZL_WC_pushHE64(ZL_WC* wc, uint64_t val)
{
    ZL_WC_ASSERT_HAS(wc, 8);
    ZS_write64(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, 8);
}

ZL_INLINE void ZL_WC_pushBE64(ZL_WC* wc, uint64_t val)
{
    ZL_WC_ASSERT_HAS(wc, 8);
    ZL_writeBE64(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, 8);
}

ZL_INLINE void ZL_WC_pushLE64(ZL_WC* wc, uint64_t val)
{
    ZL_WC_ASSERT_HAS(wc, 8);
    ZL_writeLE64(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, 8);
}

// size_t writes

ZL_INLINE void ZL_WC_pushCEST(ZL_WC* wc, size_t val)
{
    ZL_WC_ASSERT_HAS(wc, sizeof(size_t));
    ZL_writeCEST(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, sizeof(size_t));
}

ZL_INLINE void ZL_WC_pushHEST(ZL_WC* wc, size_t val)
{
    ZL_WC_ASSERT_HAS(wc, sizeof(size_t));
    ZL_writeST(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, sizeof(size_t));
}

ZL_INLINE void ZL_WC_pushBEST(ZL_WC* wc, size_t val)
{
    ZL_WC_ASSERT_HAS(wc, sizeof(size_t));
    ZL_writeBEST(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, sizeof(size_t));
}

ZL_INLINE void ZL_WC_pushLEST(ZL_WC* wc, size_t val)
{
    ZL_WC_ASSERT_HAS(wc, sizeof(size_t));
    ZL_writeLEST(ZL_WC_ptr(wc), val);
    ZL_WC_advance(wc, sizeof(size_t));
}

// varint writes

ZL_INLINE void ZL_WC_pushVarint(ZL_WC* wc, uint64_t val)
{
    ZL_WC_ASSERT_HAS(wc, ZL_varintSize(val));
    ZL_WC_advance(wc, ZL_varintEncode(val, ZL_WC_ptr(wc)));
}

// Write variable size integer.
// `val` must fit in `size` bytes.
ZL_INLINE void ZL_WC_pushHE(ZL_WC* wc, uint64_t val, size_t size)
{
    switch (size) {
        case 1:
            ZL_WC_push(wc, (uint8_t)val);
            break;
        case 2:
            ZL_WC_pushHE16(wc, (uint16_t)val);
            break;
        case 4:
            ZL_WC_pushHE32(wc, (uint32_t)val);
            break;
        case 8:
            ZL_WC_pushHE64(wc, (uint64_t)val);
            break;
        default:
            ZL_ASSERT_FAIL("Unsupported size %u", (unsigned)size);
            break;
    }
}

/**
 * Pull size bytes from the ReadCursor and push them into this WriteCursor.
 */
ZL_INLINE void ZL_WC_move(ZL_WC* wc, ZL_RC* rc, size_t size)
{
    ZL_WC_shove(wc, ZL_RC_pull(rc, size), size);
}

ZL_INLINE void ZL_WC_moveAll(ZL_WC* wc, ZL_RC* rc)
{
    ZL_WC_move(wc, rc, ZL_RC_avail(rc));
}

ZL_END_C_DECLS

#endif // ZSTRONG_COMMON_CURSOR_H
