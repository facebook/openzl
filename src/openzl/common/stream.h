// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMMON_STREAM_H
#define ZSTRONG_COMMON_STREAM_H
/** Implements methods associated with ZL_Data. */

#include "openzl/common/allocation.h"  // Allocator*
#include "openzl/common/vector.h"      // DECLARE_VECTOR_POINTERS_TYPE
#include "openzl/shared/portability.h" // ZL_BEGIN_C_DECLS
#include "openzl/zl_buffer.h"          // ZL_RBuffer/ZL_WBuffer
#include "openzl/zl_data.h"            // ZL_Data, ZL_Type
#include "openzl/zl_errors.h"          // ZL_Report codes
#include "openzl/zl_opaque_types.h"    // Stream forward decls

ZL_BEGIN_C_DECLS

/**
 * Convenience typedefs so downstream units can use VECTOR_POINTERS(ZL_Data) or
 * VECTOR_CONST_POINTERS(ZL_Data) without redeclaring them. These do not expose
 * additional Stream functionality.
 */
DECLARE_VECTOR_POINTERS_TYPE(ZL_Data)
DECLARE_VECTOR_CONST_POINTERS_TYPE(ZL_Data)

/**
 * Internal Stream interface.
 *
 * Public callers should continue to rely on the ZL_Data_* façade declared in
 * include/openzl/zl_data.h. Those entry points forward to the STREAM_* symbols
 * below. New internal code should prefer STREAM_* so that future refactors only
 * have to update a single namespace.
 */

/**
 * Stream lifecycle helpers (typical usage):
 *
 * Producer:
 *   1. STREAM_create()/STREAM_createInArena()
 *   2. STREAM_reserve()/STREAM_attach*() to obtain a writable buffer
 *   3. Populate the buffer via STREAM_wPtr()/STREAM_wStringLens()
 *   4. STREAM_commit() to publish `numElts`, STREAM_clear() to reuse
 *
 * Consumer:
 *   1. STREAM_create()/STREAM_ref*() to attach to a committed source
 *   2. Inspect metadata (STREAM_type(), STREAM_eltCount(), etc.)
 *   3. Read through STREAM_rPtr()/STREAM_rStringLens()
 *
 * Strings:
 *   - Reserve lengths with STREAM_reserveStrings()/STREAM_reserveStringLens()
 *   - Attach external length arrays via STREAM_refMutStringLens()
 */

/**
 * Allocate or destroy stream handles.
 * STREAM_create() uses an internal heap-backed arena and returns an
 * initialized stream tagged with @p id (NULL on failure).
 * STREAM_createInArena() binds the stream to caller-managed @p a, which must
 * outlive the stream.
 * STREAM_free() releases buffers and returns arena memory; safe on NULL.
 */
Stream* STREAM_create(ZL_DataID id);
Stream* STREAM_createInArena(Arena* a, ZL_DataID id);
void STREAM_free(Stream* s);

/** Allocate a typed buffer. */
ZL_Report
STREAM_reserve(Stream* s, ZL_Type type, size_t eltWidth, size_t eltCount);

/** Allocate a raw buffer to be typed later. */
ZL_Report STREAM_reserveRawBuffer(Stream* s, size_t byteCapacity);

/* ====================================================================== */
/* Writable references: attach to external buffers or type owned storage. */

/**
 * Initialize a new stream as a writable reference into an externally owned
 * buffer and set its type.
 * Typically used for the last decompression stream (write output).
 */
ZL_Report STREAM_attachWritableBuffer(
        Stream* s,
        void* buffer,
        ZL_Type type,
        size_t eltWidth,
        size_t eltCapacity);

/**
 * Initialize a new stream as a writable reference into an externally owned
 * buffer without yet setting its type.
 * The buffer will be typed later, once the output type is known, using
 * STREAM_typeAttachedBuffer().
 * Typically used for the last decompression stream (write output).
 */
ZL_Report STREAM_attachRawBuffer(Stream* s, void* rawBuf, size_t bufByteSize);

/**
 * Type a stream that already owns or references a buffer
 * but has not yet been typed.
 * @pre @p s references a writable buffer sized at least
 *      @p eltWidth * @p eltCapacity bytes.
 */
ZL_Report STREAM_typeAttachedBuffer(
        Stream* s,
        ZL_Type type,
        size_t eltWidth,
        size_t eltCapacity);

/* ========================================================= */
/* Read-only references and type tagging to external buffers */

/**
 * References the contents of @p src into @p dst as a read-only reference.
 * All original properties (type, size, metadata) are referenced.
 */
ZL_Report STREAM_refStreamWithoutRefCount(Stream* dst, const Stream* src);

/**
 * Initialize @p dst as a read-only slice of @p src using byte offsets.
 * The reference may reinterpret the element type.
 * Only the primary buffer is referenced; string streams must manage length data
 * separately.
 */
ZL_Report STREAM_refStreamByteSlice(
        Stream* dst,
        const Stream* src,
        ZL_Type type,
        size_t offsetBytes,
        size_t eltWidth,
        size_t eltCount);

/**
 * @p dst references a slice of @p src spanning @p numElts elements starting at
 * @p startingEltNum. The type remains unchanged.
 * Only safe when @p src stays stable (e.g. input buffers).
 * Callers must ensure startingEltNum + numElts <= src.numElts.
 */
ZL_Report STREAM_refStreamSliceWithoutRefCount(
        Stream* dst,
        const Stream* src,
        size_t startingEltNum,
        size_t numElts);

/**
 * @p dst references the tail of @p src starting at element @p startingEltNum.
 * Only safe when @p src remains stable (e.g. input buffers).
 * Callers must ensure startingEltNum <= src.numElts.
 */
ZL_Report STREAM_refEndStreamWithoutRefCount(
        Stream* dst,
        const Stream* src,
        size_t startingEltNum);

/**
 * Initialize a new stream as a read-only reference into an externally owned
 * buffer and set its type.
 * Typically used for the first compression stream (read input).
 */
ZL_Report STREAM_refConstBuffer(
        Stream* s,
        const void* ref,
        ZL_Type type,
        size_t eltWidth,
        size_t eltCount);

/* =================================================================== */
/* String type helpers: utilities dedicated to ZL_Type_string streams. */

/** Allocate internal buffers specifically for string streams. */
ZL_Report
STREAM_reserveStrings(Stream* s, size_t numStrings, size_t bufferCapacity);

/**
 * Initialize a new stream as a read-only reference into externally owned
 * buffers representing strings in flat format.
 * Typically used for the first stream (read input).
 */
ZL_Report STREAM_refConstExtString(
        Stream* s,
        const void* strBuffer,
        size_t bufferSize,
        const uint32_t* strLengths,
        size_t nbStrings);

/**
 * Complete an existing string stream by attaching a buffer that stores string
 * lengths.
 * The stream must already be initialized and typed, but must not yet have a
 * lengths buffer.
 * Typically used for the last decompression stream (write output).
 */
ZL_Report
STREAM_refMutStringLens(Stream* s, uint32_t* stringLens, size_t eltsCapacity);

/** Read-only access to the string length array. */
const uint32_t* STREAM_rStringLens(const Stream* s);

/** Mutable access to the string length array. */
uint32_t* STREAM_wStringLens(Stream* s);

/** Reserve space for @p nbStrings string length entries. */
uint32_t* STREAM_reserveStringLens(Stream* s, size_t nbStrings);

/* Accessors (expect a fully initialized stream unless noted). Writable
 * accessors like STREAM_wPtr require a mutable buffer on an uncommitted
 * stream. */
ZL_DataID STREAM_id(const Stream* s);
ZL_Type STREAM_type(const Stream* s);
size_t STREAM_eltCount(const Stream* s);
size_t STREAM_eltWidth(const Stream* s);
int STREAM_hasBuffer(const Stream* s);
size_t STREAM_byteSize(const Stream* s);
const void* STREAM_rPtr(const Stream* s);
void* STREAM_wPtr(Stream* s);
ZL_RBuffer STREAM_getRBuffer(const Stream* s);
ZL_WBuffer STREAM_getWBuffer(Stream* s);
int STREAM_isCommitted(const Stream* s);

/**
 * Finalize the stream after writing @p numElts elements (or strings).
 * Writers must invoke this exactly once; readers expect committed streams.
 */
ZL_Report STREAM_commit(Stream* s, size_t numElts);

/**
 * Request capacity in number of elements.
 * Note: string streams cannot derive their primary buffer capacity through this
 * helper.
 */
size_t STREAM_eltCapacity(const Stream* s);

/** Request capacity of the primary buffer in bytes. */
size_t STREAM_byteCapacity(const Stream* s);

/**
 * Lightweight metadata channel used by co-operating nodes to exchange small
 * integer hints alongside the stream payload.
 */
ZL_Report STREAM_setIntMetadata(Stream* s, int mId, int mValue);
ZL_IntMetadata STREAM_getIntMetadata(const Stream* s, int mId);

/**
 * Hash the content of all streams provided in @p streams.
 * Only meaningful when all streams have been committed.
 * Returns the low 32-bit of XXH3_64bits.
 */
ZL_Report STREAM_hashLastCommit_xxh3low32(
        const Stream* streams[],
        size_t nbStreams,
        unsigned formatVersion);

/* Bulk operations and stream actions. */

/**
 * Copy @p sizeInBytes bytes from @p src into @p dst, performing boundary
 * checks, element-width validation, and commit bookkeeping.
 * Both streams must provide sufficient capacity for the operation.
 * Intended primarily for conversion operations.
 */
ZL_Report STREAM_copyBytes(Stream* dst, const Stream* src, size_t sizeInBytes);

/**
 * Append the contents of @p src into @p dst.
 * @p src must have the same type and element width as @p dst.
 * @p dst must already own enough capacity to hold the additional elements.
 * @return Number of elements appended, or an error.
 */
ZL_Report STREAM_append(Stream* dst, const Stream* src);

/**
 * Duplicate a string stream into an empty destination stream (no buffer
 * allocated nor referenced).
 */
ZL_Report STREAM_copyStringStream(
        Stream* emptyStreamDst,
        const Stream* stringStreamSrc);

/**
 * Copy a stream from @p src to @p dst.
 * @pre @p dst must be empty and @p src must be committed.
 */
ZL_Report STREAM_copy(Stream* dst, const Stream* src);

/**
 * Consider the first @p numElts as "consumed",
 * after this operation, @p data will only reference the second unconsumed part
 * of the original @p data. Only works on already committed @p data. Primarily
 * used by Segmenters.
 */
ZL_Report STREAM_consume(Stream* data, size_t numElts);

/** Clear a stream for reuse with the same type, element width, and element
 * count. */
void STREAM_clear(Stream* s);

ZL_END_C_DECLS

#endif /* ZSTRONG_COMMON_STREAM_H */
