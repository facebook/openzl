// Copyright (c) Meta Platforms, Inc. and affiliates.

/*
 * \file
 *
 * This header is intended to replace common/buffer.h,
 * which described buffers as overlays on top of cursors.
 * I believe this dependency has it backward,
 * as I would rather see cursors being backed by buffers.
 * There are also a ton of usages which require buffers without cursors,
 * especially when the buffers are merely entirely consumed.
 *
 * Buffer definition is also needed for public (API) usage,
 * notably for custom transform developers,
 * as they are part of the interface requesting creation of streams.
 */

#ifndef ZSTRONG_COMMON_BUFFER_INTERNAL_H
#define ZSTRONG_COMMON_BUFFER_INTERNAL_H

#include "openzl/common/debug.h"
#include "openzl/common/vector.h"
#include "openzl/shared/mem.h" // ZL_memcpy
#include "openzl/shared/portability.h"
#include "openzl/zl_buffer.h" // WBuffer
#include "openzl/zl_errors.h" // ZL_Report

ZL_BEGIN_C_DECLS

// Note : naming
// We use ZS2_ prefix
// for consistency with names already defined in public zs2_buffer.h

ZL_RESULT_DECLARE_TYPE(ZL_RBuffer);

typedef struct {
    ZL_RBuffer rb;
    size_t rpos;
} ZL_RCursor;

ZL_INLINE ZL_RBuffer ZL_RBuffer_fromWCursor(ZL_WCursor wc)
{
    ZL_ASSERT_LE(wc.pos, wc.wb.capacity);
    if (wc.wb.start == NULL)
        ZL_ASSERT_EQ(wc.wb.capacity, 0);
    return (ZL_RBuffer){ .start = wc.wb.start, .size = wc.pos };
}

/// @returns A ZL_RBuffer pointing to the data in @p vec.
ZL_INLINE ZL_RBuffer ZL_RBuffer_fromVector(VECTOR(uint8_t) const* vec)
{
    return (ZL_RBuffer){ VECTOR_DATA(*vec), VECTOR_SIZE(*vec) };
}

/* ZL_WCursor_write():
 * Write the content of @src into @*wcp, and update it.
 * Note : this operation can fail, if amount to write overflows @wcp capacity.
 * @return : remaining capacity in @wcp, or an error code
 */
ZL_INLINE ZL_Report ZL_WCursor_write(ZL_WCursor* wcp, ZL_RBuffer src)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT((ZL_OperationContext*)NULL);
    ZL_ASSERT_NN(wcp);
    if (src.size)
        ZL_ASSERT_NN(src.start);
    if (wcp->pos + src.size > wcp->wb.capacity) {
        ZL_ERR(internalBuffer_tooSmall);
    }
    if (src.size) {
        ZL_memcpy((char*)wcp->wb.start + wcp->pos, src.start, src.size);
        wcp->pos += src.size;
    }
    return ZL_returnValue(wcp->wb.capacity - wcp->pos);
}

/* ZL_RCursor_RPtr():
 * @return the current reading position into the cursor as a const void* ptr
 */
ZL_INLINE const void* ZL_RCursor_RPtr(ZL_RCursor rc)
{
    ZL_ASSERT_LE(rc.rpos, rc.rb.size);
    return (const char*)rc.rb.start + rc.rpos;
}

/* ZL_RBuffer_slice():
 * @return a read-only reference to a slice of @rb.
 * Note : this operation can fail, notably if slice boundaries go beyond @rb.
 */
ZL_INLINE ZL_RESULT_OF(ZL_RBuffer)
        ZL_RBuffer_slice(ZL_RBuffer rb, size_t startPos, size_t length)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_RBuffer, (ZL_OperationContext*)NULL);
    ZL_ERR_IF_LT(startPos + length, startPos,
                 corruption); // ensure no overflow
    ZL_ERR_IF_GT(startPos + length, rb.size, corruption);
    if (rb.start == NULL)
        ZL_ASSERT_EQ(startPos, 0);
    const void* const newStart =
            startPos ? (const char*)rb.start + startPos : rb.start;
    ZL_RBuffer ret = (ZL_RBuffer){ newStart, length };
    return ZL_WRAP_VALUE(ret);
}

ZL_END_C_DECLS

#endif // ZSTRONG_COMMON_BUFFER_INTERNAL_H
