// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMMON_STREAM_H
#define ZSTRONG_COMMON_STREAM_H

// Implement methods associated with ZL_Data

#include "openzl/common/allocation.h" // Allocator*
#include "openzl/common/vector.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_buffer.h" // ZL_RBuffer
#include "openzl/zl_data.h"   // ZL_Data, ZL_Type
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"

ZL_BEGIN_C_DECLS

DECLARE_VECTOR_POINTERS_TYPE(ZL_Data)
DECLARE_VECTOR_CONST_POINTERS_TYPE(ZL_Data)

Stream* STREAM_create(ZL_DataID id);
Stream* STREAM_createInArena(Arena* a, ZL_DataID id);
void STREAM_free(Stream* s);

// Allocate a typed buffer
ZL_Report
STREAM_reserve(Stream* s, ZL_Type type, size_t eltWidth, size_t eltCount);

// Allocate a raw buffer, to be typed later
ZL_Report STREAM_reserveRawBuffer(Stream* s, size_t byteCapacity);

// Allocate internal buffers, only for String type
ZL_Report
STREAM_reserveStrings(Stream* s, size_t numStrings, size_t bufferCapacity);

/**
 * References the contents of @p src into @p dst as a read-only reference.
 * All original properties (type, size, metadata) are referenced.
 */
ZL_Report STREAM_refStreamWithoutRefcount(Stream* dst, const Stream* src);

// Init new stream, as read-only reference of a slice into an existing stream.
// The type of the reference can be different from the source.
// The slice is provided using byte length.
// It only takes care of the buffer portion of the Stream.
// String type must still take care of the array of Lenghts.
ZL_Report STREAM_refStreamByteSlice(
        Stream* dst,
        const Stream* src,
        ZL_Type type,
        size_t offsetBytes,
        size_t eltWidth,
        size_t eltCount);

/**
 * @p dst references a slice of @p src
 * of size @p numElts,
 * starting from element @p startingEltNum.
 * The type remains the same.
 * Only suitable to read into stable @p src, like user Input.
 * All parameters must be valid,
 * in particular, startingEltNum + numElts <= src.numElts
 */
ZL_Report STREAM_refStreamSliceWithoutRefCount(
        Stream* dst,
        const Stream* src,
        size_t startingEltNum,
        size_t numElts);

/**
 * @p dst references the latter part of @p src
 * starting from element @p startingEltNum.
 * Only suitable to read into stable @p src, like user Input.
 * All parameters must be valid,
 * in particular, startingEltNum <= src.numElts
 */
ZL_Report STREAM_refEndStreamWithoutRefCount(
        Stream* dst,
        const Stream* src,
        size_t startingEltNum);

// Init a new stream, as a read-only reference into an externally owned buffer
// and Type it.
// Typically used for the first compression stream (read input)
ZL_Report STREAM_refConstBuffer(
        Stream* s,
        const void* ref,
        ZL_Type type,
        size_t eltWidth,
        size_t eltCount);

// Init a new stream, as a writable reference into an externally owned buffer
// and Type it.
// Typically used for the last decompression stream (write output)
ZL_Report STREAM_refMutBuffer(
        Stream* s,
        void* buffer,
        ZL_Type type,
        size_t eltWidth,
        size_t eltCapacity);

// Complete an existing stream of type String
// and provide a buffer for the array of String Lengths.
// The Stream must be already initialized and typed,
// but not have received or allocated a buffer for String Lengths.
// Typically used for the last decompression stream (write output)
ZL_Report
STREAM_refMutStringLens(Stream* s, uint32_t* stringLens, size_t eltsCapacity);

// Init a new stream, as a writable reference into an externally owned buffer,
// but do not initialize its type yet.
// The buffer will be typed later, on discovering the output type,
// using STREAM_initWritableStream().
// Typically used for the last decompression stream (write output)
ZL_Report STREAM_refMutRawBuffer(Stream* s, void* rawBuf, size_t bufByteSize);

// Type a Stream which already owns or references a buffer.
// Typically used for the last stream (write output)
ZL_Report STREAM_initWritableStream(
        Stream* s,
        ZL_Type type,
        size_t eltWidth,
        size_t eltCapacity);

// Init a new stream, as a read-only reference into externally owned buffers
// representing Strings in flat format. Typically used for the first stream
// (read input)
ZL_Report STREAM_refConstExtString(
        Stream* s,
        const void* strBuffer,
        size_t bufferSize,
        const uint32_t* strLengths,
        size_t nbStrings);

// Accessors
ZL_DataID STREAM_id(const Stream* s);
ZL_Type STREAM_type(const Stream* s);
size_t STREAM_numElts(const Stream* s);
size_t STREAM_eltWidth(const Stream* s);
int STREAM_hasBuffer(const Stream* s);
size_t STREAM_byteSize(const Stream* s);
const void* STREAM_rPtr(const Stream* s);
void* STREAM_wPtr(Stream* s);
const uint32_t* STREAM_rStringLens(const Stream* s);
uint32_t* STREAM_wStringLens(Stream* s);
uint32_t* STREAM_reserveStringLens(Stream* s, size_t nbStrings);
ZL_RBuffer STREAM_getRBuffer(const Stream* s);
ZL_WBuffer STREAM_getWBuffer(Stream* s);
int STREAM_isCommitted(const Stream* s);
ZL_Report STREAM_commit(Stream* s, size_t numElts);

// Request capacity in nb of elts
// Note: String type can't get capacity of its primary buffer size this way
size_t STREAM_eltCapacity(const Stream* s);

// Request capacity of primary buffer in bytes
size_t STREAM_byteCapacity(const Stream* s);

ZL_Report STREAM_setIntMetadata(Stream* s, int mId, int mValue);
ZL_IntMetadata STREAM_getIntMetadata(const Stream* s, int mId);

// Hash the content of all streams provided in @streams.
// Only makes sense if all streams have already been committed.
// Return the low 32-bit of XXH3_64bits.
ZL_Report STREAM_hashLastCommit_xxh3low32(
        const Stream* streams[],
        size_t nbStreams,
        unsigned formatVersion);

// *************************************
// Actions
// *************************************

// STREAM_copyBytes
// Bundle memcpy operation, boundary checks, eltWidth multiples, and commit.
// @dst and @src must be large enough for the operation to succeed.
// Designed primarily for conversion operations
ZL_Report STREAM_copyBytes(Stream* dst, const Stream* src, size_t sizeInBytes);

/**
 * Append content of @param src into @param dst.
 * @param src must have same type and width and @param dst.
 * @param dst must be already allocated, and be large enough to host the entire
 * @param src content.
 * @return numElts added, or an error
 */
ZL_Report STREAM_append(Stream* dst, const Stream* src);

// STREAM_copyStringStream
// Duplicate a Stream, of type String (only!)
// onto an empty destination Stream (no buffer allocated nor referenced)
ZL_Report STREAM_copyStringStream(
        Stream* emptyStreamDst,
        const Stream* stringStreamSrc);

/**
 * Copy a stream from @p src to @p dst
 * @pre @p dst must be empty and @p src must be committed
 */
ZL_Report STREAM_copy(Stream* dst, const Stream* src);

/**
 * Consider the first @p numElts as "consumed",
 * after this operation, @p data will only reference the second unconsumed part
 * of the original @p data. Only works on already committed @p data. Primarily
 * used by Segmenters.
 */
ZL_Report STREAM_consume(Stream* data, size_t numElts);

// Clear a stream for reuse with the same type/eltWidth/eltCount
void STREAM_clear(Stream* s);

ZL_END_C_DECLS

#endif // ZSTRONG_COMMON_STREAM_H
