// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <stdint.h>

#include "openzl/common/allocation.h"
#include "openzl/common/assertion.h"
#include "openzl/common/limits.h"
#include "openzl/common/refcount.h" // ZS2_RefCount_*
#include "openzl/common/stream.h"
#include "openzl/shared/mem.h"                // ZL_memcpy
#include "openzl/shared/numeric_operations.h" // NUMOP_*
#include "openzl/shared/overflow.h"
#include "openzl/shared/xxhash.h"
#include "openzl/zl_buffer.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"

typedef struct {
    int mId;
    int mValue;
} IntMeta;

DECLARE_VECTOR_TYPE(IntMeta)

struct Stream_s { // exposed publicly as ZL_Data
    ZL_Refcount buffer;
    ZL_DataID id; // unique ID used to identify this Data object
    ZL_Type type;
    size_t eltWidth; // in nb of bytes
    size_t eltsCapacity;
    size_t numElts;
    size_t bufferCapacity; // required by ZL_Type_string
    size_t bufferUsed; // @note (@cyan) should it be used *only* for `string` ?
    ZL_Refcount stringLens; // ZL_Type_string only.
    int writeCommitted;
    size_t lastCommmited;
    VECTOR(IntMeta) intMetas; // Metadata (arbitrary ID+Ints)
    Arena* alloc;
};

struct ZL_Input_s {
    Stream data;
};

struct ZL_Output_s {
    Stream data;
};

// ================================
// Allocation & lifetime management
// ================================

Stream* STREAM_createInArena(Arena* a, ZL_DataID id)
{
    ZL_ASSERT_NN(a);
    Stream* const s = ALLOC_Arena_calloc(a, sizeof(*s));
    if (!s)
        return NULL;
    s->id    = id;
    s->alloc = a;
    VECTOR_INIT(s->intMetas, ZL_CONTAINER_SIZE_LIMIT);
    return s;
}

static void* isolatedStream_malloc(Arena* arena, size_t size)
{
    (void)arena;
    return ZL_malloc(size);
}
static void* isolatedStream_calloc(Arena* arena, size_t size)
{
    (void)arena;
    return ZL_calloc(size);
}
static void isolatedStream_free(Arena* arena, void* ptr)
{
    (void)arena;
    ZL_free(ptr);
}
static Arena kIsolatedStreamAllocator = {
    .malloc = isolatedStream_malloc,
    .calloc = isolatedStream_calloc,
    .free   = isolatedStream_free,
};

Stream* STREAM_create(ZL_DataID id)
{
    return STREAM_createInArena(&kIsolatedStreamAllocator, id);
}

void STREAM_free(Stream* s)
{
    if (s == NULL)
        return;
    ZL_Refcount_destroy(&s->buffer);
    ZL_Refcount_destroy(&s->stringLens);
    VECTOR_DESTROY(s->intMetas);
    ZL_ASSERT_NN(s->alloc);
    ALLOC_Arena_free(s->alloc, s);
}

// ================================
// Initialization
// ================================

/* this is probably a bad name
 * the main idea is to add Type and other metadata
 * to an existing Stream with a main buffer already allocated */
ZL_Report STREAM_initWritableStream(
        Stream* s,
        ZL_Type type,
        size_t eltWidth,
        size_t eltCapacity)
{
    ZL_DLOG(SEQ, "STREAM_initWritableStream (type:%u)", type);
    ZL_ASSERT_NN(s);
    if (s->type != 0) {
        ZL_DLOG(SEQ, "already initialized");
        ZL_RET_R_IF_NE(corruption, s->type, type);
        ZL_RET_R_IF_NE(corruption, s->eltWidth, eltWidth);
        ZL_RET_R_IF_LT(corruption, s->bufferCapacity, eltCapacity * eltWidth);
        return ZL_returnSuccess();
    }

    /* Here, buffer exists, but nothing else is initialized */
    s->type = type;
    // control eltWidth validity
    if (type == ZL_Type_serial)
        ZL_RET_R_IF_NE(
                streamParameter_invalid,
                eltWidth,
                1,
                "Serialized must set width == 1");
    if (type == ZL_Type_struct)
        ZL_RET_R_IF_EQ(
                streamParameter_invalid,
                eltWidth,
                0,
                "Struct size must be > 0");
    if (type == ZL_Type_numeric)
        ZL_RET_R_IF_NOT(
                streamParameter_invalid,
                eltWidth == 1 || eltWidth == 2 || eltWidth == 4
                        || eltWidth == 8,
                "Numeric must be width 1, 2, 4, or 8");
    ZL_ASSERT_NN(eltWidth);
    ZL_RET_R_IF_LT(
            streamCapacity_tooSmall, s->bufferCapacity / eltWidth, eltCapacity);
    s->eltWidth     = eltWidth;
    s->eltsCapacity = s->bufferCapacity / eltWidth;
    return ZL_returnSuccess();
}

ZL_Report STREAM_reserveRawBuffer(Stream* s, size_t byteCapacity)
{
    ZL_ASSERT_NN(s);
    // For the time being, only one allocation is allowed. No resizing.
    ZL_ASSERT(ZL_Refcount_null(&s->buffer));
    ZL_ASSERT_EQ(s->numElts, 0);
    ZL_ASSERT_EQ(s->bufferUsed, 0);

    void* const buffer =
            ZL_Refcount_inArena(&s->buffer, s->alloc, byteCapacity);
    ZL_RET_R_IF_NULL(
            allocation,
            buffer,
            "STREAM_reserveRawBuffer: Failed allocating stream's buffer");

    ZL_DLOG(SEQ,
            "STREAM_reserveRawBuffer: allocating buffer of byteCapacity=%zu",
            byteCapacity);
    s->bufferCapacity = byteCapacity;
    return ZL_returnSuccess();
}

ZL_Report
STREAM_reserve(Stream* s, ZL_Type type, size_t eltWidth, size_t eltsCapacity)
{
    size_t byteCapacity;
    ZL_RET_R_IF(
            allocation,
            ZL_overflowMulST(eltsCapacity, eltWidth, &byteCapacity),
            "Allocation overflows size_t");
    ZL_RET_R_IF_ERR(STREAM_reserveRawBuffer(s, byteCapacity));
    ZL_Report const r =
            STREAM_initWritableStream(s, type, eltWidth, eltsCapacity);
    if (ZL_isError(r)) {
        ZL_Refcount_destroy(&s->buffer);
        s->bufferCapacity = 0;
    }
    return r;
}

uint32_t* STREAM_reserveStringLens(Stream* stream, size_t nbStrings)
{
    ZL_DLOG(SEQ, "STREAM_reserveStringLens (nbStrings=%zu)", nbStrings);
    if (STREAM_type(stream) != ZL_Type_string)
        return NULL;
    if (!ZL_Refcount_null(&stream->stringLens))
        return NULL; // must be unused
    if (stream->writeCommitted)
        return NULL; // not committed yet
    ZL_ASSERT_NN(stream->alloc);
    uint32_t* const stringLens = ZL_Refcount_inArena(
            &stream->stringLens, stream->alloc, nbStrings * sizeof(uint32_t));
    if (stringLens == NULL) {
        ZL_DLOG(ERROR,
                "STREAM_reserveStringLens: Failed allocation of array of lengths (for %zu Strings)",
                nbStrings);
        return NULL;
    }
    stream->eltsCapacity = nbStrings;
    return ZL_Refcount_getMut(&stream->stringLens);
}

ZL_Report
STREAM_reserveStrings(Stream* s, size_t numStrings, size_t bufferCapacity)
{
    ZL_RET_R_IF_ERR(STREAM_reserveRawBuffer(s, bufferCapacity));
    ZL_ASSERT_EQ(s->type, 0);
    s->type = ZL_Type_string;

    void* const ptr = STREAM_reserveStringLens(s, numStrings);
    if (ptr == NULL) {
        ZL_Refcount_destroy(&s->buffer);
        s->bufferCapacity = 0;
    }
    return ZL_returnSuccess();
}

static ZL_Report STREAM_referenceInternal(
        Stream* s,
        ZL_Type type,
        size_t eltWidth,
        size_t eltCount,
        const void* ref)
{
    ZL_DLOG(SEQ,
            "STREAM_referenceInternal (%zu elts of width %zu)",
            eltCount,
            eltWidth);
    ZL_RET_R_IF(
            stream_wrongInit, s->writeCommitted, "Stream already committed");
    // ZL_ASSERT(!ZL_Refcount_null(&s->buffer));
    s->type = type;
    if (type == ZL_Type_serial)
        ZL_ASSERT_EQ(eltWidth, 1);
    if (type == ZL_Type_string)
        ZL_ASSERT_EQ(eltWidth, 1);
    if (type == ZL_Type_struct)
        ZL_ASSERT_GE(eltWidth, 1);
    if (type == ZL_Type_numeric)
        ZL_ASSERT(
                eltWidth == 1 || eltWidth == 2 || eltWidth == 4
                || eltWidth == 8);
    if (type == ZL_Type_numeric) {
        ZL_RET_R_IF_NOT(
                userBuffer_alignmentIncorrect,
                MEM_IS_ALIGNED_N(ref, MEM_alignmentForNumericWidth(eltWidth)),
                "provided src buffer is incorrectly aligned for numerics of width %zu bytes",
                eltWidth);
    }
    s->eltWidth = eltWidth;
    ZL_ASSERT_EQ(s->eltsCapacity, 0);
    s->bufferCapacity = eltCount * eltWidth;
    if (s->type == ZL_Type_string) {
        // do not commit yet : requires adding array of field sizes
        return ZL_returnSuccess();
    }
    s->numElts        = eltCount;
    s->bufferUsed     = s->bufferCapacity;
    s->lastCommmited  = eltCount;
    s->writeCommitted = 1; /* no longer possible to write into this stream,
                              assume it's complete */

    return ZL_returnSuccess();
}

ZL_Report STREAM_refConstBuffer(
        Stream* s,
        const void* ref,
        ZL_Type type,
        size_t eltWidth,
        size_t eltCount)
{
    ZL_ASSERT_NN(s);
    ZL_ASSERT(ZL_Refcount_null(&s->buffer));
    ZL_ASSERT_NE(type, ZL_Type_string);
    if (eltCount > 0)
        ZL_ASSERT_NN(ref);
    ZL_RET_R_IF_ERR(ZL_Refcount_initConstRef(&s->buffer, ref));
    return STREAM_referenceInternal(s, type, eltWidth, eltCount, ref);
}

ZL_Report STREAM_refConstExtString(
        Stream* s,
        const void* strBuffer,
        size_t bufferSize,
        const uint32_t* strLengths,
        size_t nbStrings)
{
    ZL_ASSERT_NN(s);
    ZL_ASSERT(ZL_Refcount_null(&s->buffer));
    ZL_ASSERT(ZL_Refcount_null(&s->stringLens));
    ZL_RET_R_IF(
            stream_wrongInit, s->writeCommitted, "Stream already committed");
    if (nbStrings)
        ZL_ASSERT_NN(strLengths);
    ZL_RET_R_IF_ERR(ZL_Refcount_initConstRef(&s->buffer, strBuffer));
    ZL_RET_R_IF_ERR(STREAM_referenceInternal(
            s, ZL_Type_string, 1, bufferSize, strBuffer));
    ZL_RET_R_IF_ERR(ZL_Refcount_initConstRef(&s->stringLens, strLengths));
    s->eltsCapacity = nbStrings;
    ZL_RET_R_IF_ERR(STREAM_commit(s, nbStrings));
    return ZL_returnSuccess();
}

ZL_Report STREAM_refMutBuffer(
        Stream* s,
        void* ref,
        ZL_Type type,
        size_t eltWidth,
        size_t eltCount)
{
    ZL_DLOG(SEQ, "STREAM_refMutBuffer (eltCount=%zu)", eltCount);
    ZL_ASSERT_NN(s);
    ZL_ASSERT(ZL_Refcount_null(&s->buffer));
    ZL_ASSERT_NE(type, ZL_Type_string); // not supported
    ZL_ASSERT_GT(eltWidth, 0);
    if (eltCount > 0)
        ZL_ASSERT_NN(ref);
    ZL_RET_R_IF_ERR(ZL_Refcount_initMutRef(&s->buffer, ref));
    s->bufferCapacity = eltCount * eltWidth;
    return STREAM_initWritableStream(s, type, eltWidth, eltCount);
}

ZL_Report
STREAM_refMutStringLens(Stream* s, uint32_t* stringLens, size_t eltsCapacity)
{
    ZL_ASSERT_NN(s);
    ZL_RET_R_IF_NE(streamType_incorrect, s->type, ZL_Type_string);
    ZL_RET_R_IF_NOT(stream_wrongInit, ZL_Refcount_null(&s->stringLens));
    if (eltsCapacity > 0)
        ZL_ASSERT_NN(stringLens);
    ZL_RET_R_IF_ERR(ZL_Refcount_initMutRef(&s->stringLens, stringLens));
    s->eltsCapacity = eltsCapacity;
    return ZL_returnSuccess();
}

ZL_Report STREAM_refMutRawBuffer(Stream* s, void* rawBuf, size_t bufByteSize)
{
    ZL_DLOG(SEQ, "STREAM_refMutRawBuffer (bufByteSize=%zu)", bufByteSize);
    ZL_ASSERT_NN(s);
    ZL_ASSERT(ZL_Refcount_null(&s->buffer));
    if (bufByteSize > 0)
        ZL_ASSERT_NN(rawBuf);
    ZL_RET_R_IF_ERR(ZL_Refcount_initMutRef(&s->buffer, rawBuf));
    s->bufferCapacity = bufByteSize;
    return ZL_returnSuccess();
}

ZL_Report STREAM_refStreamWithoutRefCount(Stream* s, const Stream* ref)
{
    ZL_ASSERT_NN(s);
    ZL_ASSERT_NN(ref);
    ZL_ASSERT(ref->writeCommitted);
    ZL_RET_R_IF(
            stream_wrongInit, s->writeCommitted, "Stream already committed");
    s->type           = ref->type;
    s->numElts        = ref->numElts;
    s->eltWidth       = ref->eltWidth;
    s->bufferCapacity = ref->bufferCapacity;
    s->bufferUsed     = ref->bufferUsed;
    s->lastCommmited  = ref->numElts;
    s->writeCommitted = 1;

    // Copy the stream metadata
    size_t meta_size = VECTOR_SIZE(ref->intMetas);
    VECTOR_CLEAR(s->intMetas);
    if (VECTOR_RESERVE(s->intMetas, meta_size) < meta_size) {
        return ZL_REPORT_ERROR(allocation, "Failed to reserve metadata");
    }
    for (size_t pos = 0; pos < meta_size; pos++) {
        IntMeta e = VECTOR_AT(ref->intMetas, pos);
        if (!VECTOR_PUSHBACK(s->intMetas, e)) {
            return ZL_REPORT_ERROR(allocation, "Failed to copy metadata");
        }
    }

    ZL_RET_R_IF_ERR(ZL_Refcount_initConstRef(
            &s->buffer, ZL_Refcount_get(&ref->buffer)));
    ZL_RET_R_IF_ERR(ZL_Refcount_initConstRef(
            &s->stringLens, ZL_Refcount_get(&ref->stringLens)));

    // Turn our buffers into immutable references
    ZL_Refcount_constify(&s->buffer);
    ZL_Refcount_constify(&s->stringLens);

    return ZL_returnSuccess();
}

ZL_Report STREAM_refStreamByteSlice(
        Stream* dst,
        const Stream* src,
        ZL_Type type,
        size_t offsetBytes,
        size_t eltWidth,
        size_t eltCount)
{
    size_t const streamBytes = STREAM_byteSize(src);
    size_t neededBytes;
    ZL_RET_R_IF(
            allocation,
            ZL_overflowMulST(eltCount, eltWidth, &neededBytes),
            "Size overflows size_t");
    ZL_RET_R_IF(
            allocation,
            ZL_overflowAddST(offsetBytes, neededBytes, &neededBytes),
            "Size overflows size_t");
    ZL_RET_R_IF_GT(allocation, neededBytes, streamBytes);
    dst->buffer = ZL_Refcount_aliasOffset(&src->buffer, offsetBytes);
    // Turn our buffer into an immutable reference
    ZL_Refcount_constify(&dst->buffer);
    return STREAM_referenceInternal(
            dst, type, eltWidth, eltCount, ZL_Refcount_get(&dst->buffer));
}

/** At this point, @p dst is expected to have been initialized with
 * STREAM_refStreamWithoutRefCount(), which means it is by now a reference to
 * the entire @p src. The work is to reduce the range to just the wanted slice.
 */
static ZL_Report STREAM_refStreamStringSlice(
        Stream* dst,
        const Stream* src,
        size_t startingEltNum,
        size_t numElts)
{
    ZL_ASSERT_NN(src);
    ZL_ASSERT_EQ(STREAM_type(src), ZL_Type_string);
    ZL_ASSERT_GE(STREAM_numElts(src), startingEltNum + numElts);

    uint64_t const skipped =
            NUMOP_sumArray32(ZL_Refcount_get(&src->stringLens), startingEltNum);
    uint64_t const totalStringSizes = NUMOP_sumArray32(
            (const uint32_t*)ZL_Refcount_get(&src->stringLens) + startingEltNum,
            numElts);
    ZL_ASSERT_NN(dst);
    ZL_ASSERT_EQ(STREAM_type(dst), ZL_Type_string);
    ZL_ASSERT(dst->buffer._ptr == src->buffer._ptr);
    dst->buffer._ptr = (char*)dst->buffer._ptr + skipped;
    ZL_ASSERT_GE(dst->numElts, numElts);
    dst->numElts       = numElts;
    dst->lastCommmited = numElts;
    ZL_ASSERT_GE(dst->bufferCapacity, totalStringSizes);
    dst->bufferCapacity = totalStringSizes;
    dst->bufferUsed     = totalStringSizes;
    ZL_ASSERT(dst->writeCommitted);
    return ZL_returnSuccess();
}

/* Conditions:
 * All parameters are valid, notably:
 * - dst and src are != NULL
 * - startingEltNum + numElts <= src.numElts
 */
ZL_Report STREAM_refStreamSliceWithoutRefCount(
        Stream* dst,
        const Stream* src,
        size_t startingEltNum,
        size_t numElts)
{
    ZL_DLOG(SEQ,
            "STREAM_refStreamSliceWithoutRefCount (start:%zu, numElts=%zu)",
            startingEltNum,
            numElts);
    ZL_ASSERT_NN(src);
    ZL_ASSERT_LE(startingEltNum + numElts, STREAM_numElts(src));
    ZL_ASSERT_NN(dst);
    ZL_RET_R_IF_ERR(STREAM_refStreamWithoutRefCount(dst, src));
    if (numElts == STREAM_numElts(src))
        return ZL_returnSuccess();

    if (STREAM_type(src) == ZL_Type_string) {
        return STREAM_refStreamStringSlice(dst, src, startingEltNum, numElts);
    }

    size_t const eltWidth = STREAM_eltWidth(dst);
    ZL_ASSERT_NN(eltWidth);
    dst->buffer._ptr    = (char*)dst->buffer._ptr + startingEltNum * eltWidth;
    dst->numElts        = numElts;
    dst->lastCommmited  = numElts;
    dst->bufferCapacity = numElts * eltWidth;
    dst->bufferUsed     = numElts * eltWidth;
    return ZL_returnSuccess();
}

/* Conditions:
 * All parameters are valid, notably:
 * - dst and src are != NULL
 * - startingEltNum <= src.numElts
 */
ZL_Report STREAM_refEndStreamWithoutRefCount(
        Stream* dst,
        const Stream* src,
        size_t startingEltNum)
{
    ZL_DLOG(SEQ, "STREAM_refStreamFrom (start:%zu)", startingEltNum);
    ZL_ASSERT_NN(src);
    ZL_ASSERT_LE(startingEltNum, STREAM_numElts(src));
    size_t numElts = STREAM_numElts(src) - startingEltNum;
    return STREAM_refStreamSliceWithoutRefCount(
            dst, src, startingEltNum, numElts);
}

// ================================
// Accessors
// ================================

ZL_DataID STREAM_id(const Stream* in)
{
    return in->id;
}

ZL_Type STREAM_type(const Stream* in)
{
    ZL_ASSERT_NN(in);
    ZL_ASSERT(
            in->type == ZL_Type_unassigned || in->type == ZL_Type_serial
            || in->type == ZL_Type_struct || in->type == ZL_Type_numeric
            || in->type == ZL_Type_string);
    return in->type;
}

size_t STREAM_eltWidth(const Stream* in)
{
    ZL_ASSERT_NN(in);
    if (in->type == ZL_Type_string)
        return 0;
    return in->eltWidth;
}

size_t STREAM_eltCapacity(const Stream* in)
{
    ZL_ASSERT_NN(in);
    return in->eltsCapacity - in->numElts; // remaining capacity
}

size_t STREAM_byteCapacity(const Stream* in)
{
    ZL_ASSERT_NN(in);
    return in->bufferCapacity - in->bufferUsed;
}

static ZL_RBuffer STREAM_lastCommittedStringLens(const Stream* in)
{
    ZL_ASSERT_NN(in);
    ZL_ASSERT(in->writeCommitted);
    size_t const numStrings = in->lastCommmited;
    ZL_ASSERT_LE(numStrings, in->numElts);
    size_t startElt = in->numElts - numStrings;
    if (in->stringLens._ptr == NULL) {
        ZL_ASSERT_EQ(startElt, 0);
        ZL_ASSERT_EQ(numStrings, 0);
        return (ZL_RBuffer){ NULL, 0 };
    }
    return (ZL_RBuffer){
        .start = (const uint32_t*)in->stringLens._ptr + startElt,
        .size  = numStrings * sizeof(uint32_t),
    };
}

static ZL_RBuffer STREAM_lastCommittedStringContent(const Stream* in)
{
    ZL_ASSERT_NN(in);
    ZL_ASSERT(in->writeCommitted);
    size_t const numStrings = in->lastCommmited;
    ZL_ASSERT_LE(numStrings, in->numElts);
    size_t startElt                 = in->numElts - numStrings;
    uint64_t const totalStringsSize = NUMOP_sumArray32(
            (const uint32_t*)ZL_Refcount_get(&in->stringLens) + startElt,
            numStrings);
    ZL_ASSERT_LE(totalStringsSize, in->bufferUsed);
    return (ZL_RBuffer){
        .start = (char*)in->buffer._ptr + in->bufferUsed - totalStringsSize,
        .size  = totalStringsSize,
    };
}

static ZL_RBuffer STREAM_lastCommittedBufferContent(const Stream* in)
{
    ZL_DLOG(SEQ, "STREAM_lastCommittedBufferContent");
    ZL_ASSERT_NN(in);
    if (in->writeCommitted == 0) {
        ZL_ASSERT_EQ(in->numElts, 0);
        ZL_ASSERT_EQ(in->lastCommmited, 0);
    }
    size_t const numElts = in->lastCommmited;
    ZL_ASSERT_LE(numElts, in->numElts);
    if (numElts == in->numElts) {
        // easy solution: whole buffer
        return (ZL_RBuffer){ .start = in->buffer._ptr, .size = in->bufferUsed };
    }
    /// return the last portion of @p in
    if (STREAM_type(in) == ZL_Type_string) {
        return STREAM_lastCommittedStringContent(in);
    }
    size_t startElt = in->numElts - numElts;
    return (ZL_RBuffer){
        .start = (char*)in->buffer._ptr + startElt * in->eltWidth,
        .size  = numElts * in->eltWidth,
    };
}

size_t STREAM_numElts(const Stream* in)
{
    ZL_ASSERT_NN(in);
    if (ZL_Refcount_mutable(&in->buffer))
        ZL_ASSERT_LE(in->numElts, in->eltsCapacity);
    return in->numElts;
}

size_t STREAM_byteSize(const Stream* s)
{
    ZL_ASSERT_NN(s);
    if (!s->writeCommitted) {
        ZL_DLOG(SEQ, "STREAM_byteSize: not committed !");
        /* @note It shouldn't make sense to call this function when the
         * stream is not committed yet. It's still an open question how we would
         * like to advise users against this pattern.
         * For the time being, just answer 0. */
        ZL_ASSERT_EQ(s->numElts, 0);
        ZL_ASSERT_EQ(s->bufferUsed, 0);
        ZL_ASSERT_EQ(s->lastCommmited, 0);
        return 0;
    }
    ZL_DLOG(SEQ, "STREAM_byteSize (bufferUsed=%zu)", s->bufferUsed);
    if (s->type != ZL_Type_string) {
        ZL_ASSERT_EQ(s->bufferUsed, s->eltWidth * s->numElts);
    }
    ZL_ASSERT_GE(s->bufferCapacity, s->bufferUsed);
    return s->bufferUsed;
}

int STREAM_isCommitted(const Stream* s)
{
    ZL_ASSERT_NN(s);
    ZL_ASSERT(s->writeCommitted == 0 || s->writeCommitted == 1);
    return s->writeCommitted;
}

const void* STREAM_rPtr(const Stream* in)
{
    if (in == NULL || ZL_Refcount_null(&in->buffer))
        return NULL;
    return ZL_Refcount_get(&in->buffer);
}

void* STREAM_wPtr(Stream* s)
{
    if (s == NULL || ZL_Refcount_null(&s->buffer))
        return NULL;
    void* basePtr = ZL_Refcount_getMut(&s->buffer);
    ZL_ASSERT_LE(s->bufferUsed, s->bufferCapacity);
    return (char*)basePtr + s->bufferUsed;
}

ZL_RBuffer STREAM_getRBuffer(const Stream* s)
{
    ZL_ASSERT_NN(s);
    size_t const sizeInBytes = STREAM_byteSize(s);
    ZL_DLOG(SEQ, "STREAM_getRBuffer (size=%zu)", sizeInBytes);
    return (ZL_RBuffer){
        .start = STREAM_rPtr(s),
        .size  = sizeInBytes,
    };
}

static size_t STREAM_getBufferCapacity(const Stream* s)
{
    ZL_ASSERT_NN(s);
    if (STREAM_byteCapacity(s)) {
        ZL_ASSERT_NULL(s->bufferUsed);
        ZL_ASSERT_NULL(STREAM_numElts(s));
    }
    ZL_ASSERT_LE(s->bufferUsed, s->bufferCapacity);
    return s->bufferCapacity - s->bufferUsed;
}

ZL_WBuffer STREAM_getWBuffer(Stream* s)
{
    ZL_ASSERT_NN(s);
    ZL_ASSERT_NN(STREAM_wPtr(s));
    return (ZL_WBuffer){
        .start    = STREAM_wPtr(s),
        .capacity = STREAM_getBufferCapacity(s),
    };
}

ZL_Report STREAM_hashLastCommit_xxh3low32(
        const Stream* streams[],
        size_t nbStreams,
        unsigned formatVersion)
{
    ZL_DLOG(SEQ,
            "STREAM_hashLastCommit_xxh3low32 (nbStreams=%zu, formatVersion=%u)",
            nbStreams,
            formatVersion);
    ZL_ASSERT_GT(nbStreams, 0);
    ZL_ASSERT_NN(streams);
    XXH3_state_t xxh3;
    ZL_RET_R_IF_NE(GENERIC, XXH3_64bits_reset(&xxh3), XXH_OK);
    for (size_t n = 0; n < nbStreams; n++) {
        // Hashing content only makes sense if content has been committed
        ZL_RET_R_IF_NOT(GENERIC, STREAM_isCommitted(streams[n]));
        // Numeric data might have a different endianness depending on the
        // platform which might lead to checksum errors.
        // For that reason, one convention must be selected, so that
        // checksum generates same value on all platforms.
        // The convention is Little-Endian.
        // For now, the library is not able calculate checksum on Numeric
        // input on non-little-endian platforms
        if (STREAM_type(streams[n]) == ZL_Type_numeric) {
            ZL_RET_R_IF_NOT(
                    temporaryLibraryLimitation,
                    ZL_isLittleEndian(),
                    "Cannot calculate hash of numeric input on non little-endian platforms");
        }
        ZL_RBuffer const rb = STREAM_lastCommittedBufferContent(streams[n]);
        ZL_RET_R_IF_NE(
                GENERIC, XXH3_64bits_update(&xxh3, rb.start, rb.size), XXH_OK);
        if ((STREAM_type(streams[n]) == ZL_Type_string)
            && (formatVersion >= 15)) {
            /** @note format v14 supports Type String, but did not checksum the
             * array of lengths (just skipping it) */
            ZL_RBuffer const lcslb = STREAM_lastCommittedStringLens(streams[n]);
            ZL_RET_R_IF_NE(
                    GENERIC,
                    XXH3_64bits_update(&xxh3, lcslb.start, lcslb.size),
                    XXH_OK);
        }
    }
    uint32_t const hash = (uint32_t)XXH3_64bits_digest(&xxh3);
    return ZL_returnValue(hash);
}

// **********************************
// Actions
// **********************************

static ZL_Report STREAM_commitStrings(Stream* s, size_t numStrings)
{
    ZL_DLOG(SEQ, "STREAM_commitStrings (numStrings=%zu)", numStrings);
    ZL_ASSERT_NN(s);
    ZL_ASSERT_EQ(s->type, ZL_Type_string);

    ZL_RET_R_IF_GT(
            streamCapacity_tooSmall,
            numStrings,
            s->eltsCapacity,
            "Number of strings committed is greater than capacity");
    uint64_t const totalStringsSize =
            NUMOP_sumArray32(ZL_Refcount_get(&s->stringLens), numStrings);
    ZL_RET_R_IF_GT(
            streamCapacity_tooSmall,
            totalStringsSize,
            (uint64_t)s->bufferCapacity,
            "Total string content size is greater than capacity");

    // All conditions fulfilled : now set
    s->numElts += numStrings;
    s->lastCommmited = numStrings;
    s->bufferUsed += totalStringsSize;
    s->writeCommitted = 1;
    return ZL_returnSuccess();
}

ZL_Report STREAM_commit(Stream* s, size_t numElts)
{
    ZL_DLOG(SEQ, "STREAM_commit (numElts=%zu)", numElts);
    ZL_ASSERT_NN(s);
    if (s->writeCommitted == 0) {
        ZL_ASSERT_EQ(s->numElts, 0);
        ZL_ASSERT_EQ(s->bufferUsed, 0);
    }
    ZL_RET_R_IF_GT(
            stream_wrongInit,
            s->numElts + numElts,
            s->eltsCapacity,
            "Stream capacity too small");
    if (s->type == ZL_Type_string) {
        return STREAM_commitStrings(s, numElts);
    }
    // Not String type
    s->numElts += numElts;
    s->lastCommmited = numElts;
    s->bufferUsed += numElts * s->eltWidth;
    s->writeCommitted = 1;
    ZL_DLOG(SEQ, "STREAM_commit: new total numElts=%zu", s->numElts);
    return ZL_returnSuccess();
}

const uint32_t* STREAM_rStringLens(const Stream* stream)
{
    ZL_ASSERT_NN(stream);
    if (STREAM_type(stream) != ZL_Type_string) {
        return NULL;
    }
    return ZL_Refcount_get(&stream->stringLens);
}

uint32_t* STREAM_wStringLens(Stream* stream)
{
    ZL_ASSERT_NN(stream);
    if (STREAM_type(stream) != ZL_Type_string) {
        // Note(@Cyan): in some future, we might be able to attach the error log
        // to the @p stream object, for later retrieval
        ZL_DLOG(ERROR,
                "Incorrect request : requesting write access into the String Lengths array "
                "from a Stream of different type (%u != %u)",
                stream->type,
                ZL_Type_string);
        return NULL;
    }
    if (stream->writeCommitted == 0) {
        ZL_ASSERT(stream->numElts == 0);
    }
    return (uint32_t*)ZL_Refcount_getMut(&stream->stringLens) + stream->numElts;
}

void STREAM_clear(Stream* s)
{
    ZL_ASSERT_NN(s);
    s->writeCommitted = 0;
    s->numElts        = 0;
    s->lastCommmited  = 0;
    s->bufferUsed     = 0;
}

/* Only works for elts of fixed width */
static ZL_Report STREAM_addElts(
        Stream* dst,
        const void* eltBuffer,
        size_t numElts,
        size_t eltWidth)
{
    ZL_DLOG(SEQ, "STREAM_addElts (numElts=%zu)", numElts);
    ZL_ASSERT_NN(dst);
    ZL_ASSERT_NE(STREAM_type(dst), ZL_Type_string);
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_NE(
            dst->eltWidth,
            eltWidth,
            parameter_invalid,
            "invalid width: must be identical to target stream");
    ZL_ERR_IF_GT(numElts, STREAM_eltCapacity(dst), dstCapacity_tooSmall);
    size_t addedSize = numElts * eltWidth;
    if (dst->writeCommitted == 0) {
        ZL_ASSERT_EQ(dst->bufferUsed, 0);
        ZL_ASSERT_EQ(dst->numElts, 0);
    }
    if (addedSize > 0) {
        ZL_ASSERT_LE(dst->bufferUsed, dst->bufferCapacity);
        ZL_memcpy(STREAM_wPtr(dst), eltBuffer, addedSize);
    }
    return STREAM_commit(dst, numElts);
}

/* append variant dedicated to String Type */
static ZL_Report STREAM_appendStrings(Stream* dst, const Stream* src)
{
    ZL_ASSERT_NN(dst);
    ZL_ASSERT_NN(src);
    ZL_ASSERT_EQ(STREAM_type(dst), ZL_Type_string);
    ZL_ASSERT_EQ(STREAM_type(src), ZL_Type_string);
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    size_t const numStrings = STREAM_numElts(src);
    ZL_ERR_IF_GT(numStrings, STREAM_eltCapacity(dst), dstCapacity_tooSmall);
    size_t const toCopy = STREAM_byteSize(src);
    ZL_ERR_IF_GT(toCopy, STREAM_byteCapacity(dst), dstCapacity_tooSmall);
    if (numStrings > 0) {
        ZL_memcpy(STREAM_wPtr(dst), STREAM_rPtr(src), toCopy);
        ZL_memcpy(
                STREAM_wStringLens(dst),
                STREAM_rStringLens(src),
                numStrings * sizeof(uint32_t));
    }
    return STREAM_commit(dst, numStrings);
}

ZL_Report STREAM_append(Stream* dst, const Stream* src)
{
    ZL_ASSERT_NN(src);
    ZL_DLOG(SEQ, "STREAM_append (numElts=%zu)", STREAM_numElts(src));
    ZL_ASSERT_NN(dst);
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_NE(
            STREAM_type(dst),
            STREAM_type(src),
            parameter_invalid,
            "invalid type: must be identical to target stream");
    if (STREAM_type(dst) == ZL_Type_string) {
        return STREAM_appendStrings(dst, src);
    }
    /* serial, struct and numeric */
    return STREAM_addElts(
            dst, STREAM_rPtr(src), STREAM_numElts(src), STREAM_eltWidth(src));
}

ZL_Report STREAM_copyBytes(Stream* dst, const Stream* src, size_t size)
{
    ZL_DLOG(BLOCK, "STREAM_copyBytes (%zu bytes)", size);
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ASSERT_NN(dst);
    size_t const eltWidth    = STREAM_eltWidth(dst);
    size_t const dstCapacity = STREAM_byteCapacity(dst);
    ZL_ASSERT_NN(src);
    size_t const srcSizeMax = STREAM_byteSize(src);
    ZL_ERR_IF_GT(size, dstCapacity, dstCapacity_tooSmall);
    ZL_ERR_IF_GT(size, srcSizeMax, srcSize_tooSmall);
    // size must be a strict multiple of eltWidth
    ZL_ASSERT(eltWidth != 0);
    ZL_ERR_IF_NE(size % eltWidth, 0, parameter_invalid);
    size_t const numElts = size / eltWidth;
    return STREAM_addElts(dst, STREAM_rPtr(src), numElts, eltWidth);
}

ZL_Report STREAM_copyStringStream(Stream* dst, const Stream* src)
{
    ZL_ASSERT_NN(dst);
    ZL_ASSERT_NN(src);
    ZL_ASSERT(!STREAM_hasBuffer(dst));
    ZL_ASSERT_EQ(STREAM_type(src), ZL_Type_string);
    size_t const nbStrings        = STREAM_numElts(src);
    size_t const stringsTotalSize = STREAM_byteSize(src);

    ZL_RET_R_IF_ERR(STREAM_reserve(dst, ZL_Type_string, 1, stringsTotalSize));

    uint32_t* const lens = STREAM_reserveStringLens(dst, nbStrings);
    ZL_RET_R_IF_NULL(allocation, lens);

    ZL_memcpy(STREAM_wPtr(dst), STREAM_rPtr(src), stringsTotalSize);
    ZL_memcpy(lens, STREAM_rStringLens(src), nbStrings * sizeof(uint32_t));

    ZL_RET_R_IF_ERR(STREAM_commit(dst, nbStrings));
    return ZL_returnValue(stringsTotalSize);
}

static ZL_Report STREAM_copyIntMetas(Stream* dst, const Stream* src)
{
    ZL_ASSERT_NN(dst);
    ZL_ASSERT_NN(src);

    size_t const meta_size = VECTOR_SIZE(src->intMetas);
    VECTOR_CLEAR(dst->intMetas);
    ZL_RET_R_IF_LT(
            allocation, VECTOR_RESERVE(dst->intMetas, meta_size), meta_size);

    for (size_t pos = 0; pos < meta_size; pos++) {
        IntMeta e = VECTOR_AT(src->intMetas, pos);
        ZL_RET_R_IF_NOT(allocation, VECTOR_PUSHBACK(dst->intMetas, e));
    }

    return ZL_returnSuccess();
}

ZL_Report STREAM_copy(Stream* dst, const Stream* src)
{
    ZL_ASSERT(!STREAM_hasBuffer(dst));
    ZL_ASSERT(src->writeCommitted);
    const ZL_Type type = STREAM_type(src);

    ZL_RET_R_IF_ERR(STREAM_copyIntMetas(dst, src));

    if (type == ZL_Type_string) {
        return STREAM_copyStringStream(dst, src);
    }

    ZL_RET_R_IF_ERR(STREAM_reserve(
            dst, type, STREAM_eltWidth(src), STREAM_numElts(src)));
    ZL_RET_R_IF_ERR(STREAM_copyBytes(dst, src, STREAM_byteSize(src)));
    return ZL_returnSuccess();
}

// data must be valid
// numElts must be <= numElts(data)
static ZL_Report STREAM_consumeStrings(Stream* data, size_t numElts)
{
    ZL_ASSERT_NN(data);
    ZL_ASSERT_LE(numElts, STREAM_numElts(data));
    // unfinished
    ZL_RET_R_IF(GENERIC, 1);
}

// data must be valid
// numElts must be <= numElts(data)
ZL_Report STREAM_consume(Stream* data, size_t numElts)
{
    ZL_ASSERT_NN(data);
    ZL_ASSERT_EQ(data->writeCommitted, 1);
    ZL_RET_R_IF_GT(parameter_invalid, numElts, STREAM_numElts(data));
    if (STREAM_type(data) == ZL_Type_string)
        return STREAM_consumeStrings(data, numElts);
    size_t eltSize    = STREAM_eltWidth(data);
    data->buffer._ptr = (char*)data->buffer._ptr + (numElts * eltSize);
    data->numElts -= numElts;
    data->bufferCapacity = data->numElts * eltSize;
    return ZL_returnSuccess();
}

// Metadata

// findIntMeta() :
// @return index of the Int Metadata of provided @id
// @return -1 if not found
static int findIntMeta(VECTOR(IntMeta) m, int id)
{
    size_t const nbIntMetas = VECTOR_SIZE(m);
    // Scan backward, find latest .id if multiple present
    for (int pos = (int)nbIntMetas - 1; pos >= 0; pos--) {
        if (VECTOR_DATA(m)[pos].mId == id)
            return pos;
    }
    // not found
    return -1;
}

ZL_Report STREAM_setIntMetadata(Stream* s, int mId, int mValue)
{
    ZL_ASSERT_NN(s);
    // Currently forbids setting same metadata ID multiple times
    ZL_RET_R_IF_NE(
            streamParameter_invalid,
            findIntMeta(s->intMetas, mId),
            -1,
            "Int Metadata ID already present");
    ZL_RET_R_IF_NOT(
            allocation,
            VECTOR_PUSHBACK(s->intMetas, ((IntMeta){ mId, mValue })));
    return ZL_returnSuccess();
}

#define ZS2_INTMETADATA_NOT_PRESENT (-1)
ZL_IntMetadata STREAM_getIntMetadata(const Stream* s, int mId)
{
    ZL_ASSERT_NN(s);
    int const idx = findIntMeta(s->intMetas, mId);
    if (idx < 0)
        return (ZL_IntMetadata){
            .isPresent = 0,
            .mValue    = ZS2_INTMETADATA_NOT_PRESENT,
        };
    return (ZL_IntMetadata){
        .isPresent = 1,
        .mValue    = VECTOR_DATA(s->intMetas)[idx].mValue,
    };
}

int STREAM_hasBuffer(const Stream* s)
{
    return !ZL_Refcount_null(&s->buffer);
}

// --------------------------------
// ZL_Data compatibility wrappers
// --------------------------------

uint32_t* ZL_Data_reserveStringLens(ZL_Data* stream, size_t nbStrings)
{
    return STREAM_reserveStringLens(stream, nbStrings);
}

ZL_DataID ZL_Data_id(const ZL_Data* data)
{
    return STREAM_id(data);
}

ZL_Type ZL_Data_type(const ZL_Data* data)
{
    return STREAM_type(data);
}

size_t ZL_Data_eltWidth(const ZL_Data* data)
{
    return STREAM_eltWidth(data);
}

size_t ZL_Data_numElts(const ZL_Data* data)
{
    return STREAM_numElts(data);
}

size_t ZL_Data_contentSize(const ZL_Data* data)
{
    return STREAM_byteSize(data);
}

const void* ZL_Data_rPtr(const ZL_Data* data)
{
    return STREAM_rPtr(data);
}

void* ZL_Data_wPtr(ZL_Data* data)
{
    return STREAM_wPtr(data);
}

ZL_Report ZL_Data_commit(ZL_Data* data, size_t numElts)
{
    return STREAM_commit(data, numElts);
}

const uint32_t* ZL_Data_rStringLens(const ZL_Data* stream)
{
    return STREAM_rStringLens(stream);
}

uint32_t* ZL_Data_wStringLens(ZL_Data* stream)
{
    return STREAM_wStringLens(stream);
}

ZL_Report ZL_Data_setIntMetadata(ZL_Data* stream, int mId, int mValue)
{
    return STREAM_setIntMetadata(stream, mId, mValue);
}

ZL_IntMetadata ZL_Data_getIntMetadata(const ZL_Data* stream, int mId)
{
    return STREAM_getIntMetadata(stream, mId);
}

/* ======    TypedBuffer interface    ====== */

/* Note: for the time being, TypedBuffer is the same as Stream.
 * This may change in the future, but for the time being,
 * its methods are just thin wrappers around ZS2_Data_*() methods.
 * As a consequence, these methods are hosted in `stream.c`.
 * */

ZL_TypedBuffer* ZL_TypedBuffer_create(void)
{
    ZL_DLOG(SEQ, "ZL_TypedBuffer_create");
    return ZL_codemodDataAsOutput(STREAM_create(ZL_DATA_ID_INPUTSTREAM));
}

ZL_TypedBuffer* ZL_TypedBuffer_createWrapString(
        void* stringBuffer,
        size_t stringBufferCapacity,
        uint32_t* lenBuffer,
        size_t maxNumStrings)
{
    Stream* const stream = STREAM_create(ZL_DATA_ID_INPUTSTREAM);
    if (stream == NULL)
        return NULL;
    ZL_ASSERT(ZL_Refcount_null(&stream->buffer));
    if (stringBufferCapacity > 0)
        ZL_ASSERT_NN(stringBuffer);
    if (ZL_isError(ZL_Refcount_initMutRef(&stream->buffer, stringBuffer))) {
        STREAM_free(stream);
        return NULL;
    }
    stream->bufferCapacity = stringBufferCapacity;
    stream->type           = ZL_Type_string;

    if (ZL_isError(STREAM_refMutStringLens(stream, lenBuffer, maxNumStrings))) {
        STREAM_free(stream);
        return NULL;
    }
    // Note: currently, ZL_TypedBuffer == ZL_Data
    return ZL_codemodDataAsOutput(stream);
}

static ZL_TypedBuffer*
ZL_wrapGeneric(ZL_Type type, size_t eltWidth, size_t numElts, void* src)
{
    Stream* const stream = STREAM_create(ZL_DATA_ID_INPUTSTREAM);
    if (stream == NULL)
        return NULL;
    ZL_Report ret = STREAM_refMutBuffer(stream, src, type, eltWidth, numElts);
    if (ZL_isError(ret)) {
        STREAM_free(stream);
        return NULL;
    }
    // Note: currently, ZL_TypedBuffer == ZL_Data
    return ZL_codemodDataAsOutput(stream);
}

ZL_Report ZL_Output_eltWidth(const ZL_Output* output)
{
    if (ZL_Output_type(output) != ZL_Type_string) {
        ZL_RET_R_IF_EQ(outputNotReserved, output->data.eltWidth, 0);
    }
    return ZL_returnValue(output->data.eltWidth);
}

ZL_Report ZL_Output_numElts(const ZL_Output* output)
{
    ZL_RET_R_IF_EQ(outputNotCommitted, output->data.writeCommitted, 0);
    return ZL_returnValue(output->data.numElts);
}

ZL_Report ZL_Output_contentSize(const ZL_Output* output)
{
    ZL_RET_R_IF_NOT(outputNotCommitted, STREAM_isCommitted(&output->data));
    return ZL_returnValue(STREAM_byteSize(&output->data));
}

ZL_Report ZL_Output_eltsCapacity(const ZL_Output* output)
{
    ZL_RET_R_IF(outputNotReserved, !STREAM_hasBuffer(&output->data));
    return ZL_returnValue(output->data.eltsCapacity);
}

ZL_Report ZL_Output_contentCapacity(const ZL_Output* output)
{
    ZL_RET_R_IF(outputNotReserved, !STREAM_hasBuffer(&output->data));
    return ZL_returnValue(output->data.bufferCapacity);
}

ZL_TypedBuffer* ZL_TypedBuffer_createWrapSerial(void* src, size_t srcSize)
{
    return ZL_wrapGeneric(ZL_Type_serial, 1, srcSize, src);
}

ZL_TypedBuffer*
ZL_TypedBuffer_createWrapStruct(void* src, size_t eltWidth, size_t numElts)
{
    return ZL_wrapGeneric(ZL_Type_struct, eltWidth, numElts, src);
}

ZL_TypedBuffer*
ZL_TypedBuffer_createWrapNumeric(void* src, size_t eltWidth, size_t numElts)
{
    return ZL_wrapGeneric(ZL_Type_numeric, eltWidth, numElts, src);
}

void ZL_TypedBuffer_free(ZL_TypedBuffer* tbuffer)
{
    STREAM_free(ZL_codemodOutputAsData(tbuffer));
}

ZL_Type ZL_TypedBuffer_type(const ZL_TypedBuffer* tbuffer)
{
    return STREAM_type(ZL_codemodConstOutputAsData(tbuffer));
}

const void* ZL_TypedBuffer_rPtr(const ZL_TypedBuffer* tbuffer)
{
    return STREAM_rPtr(ZL_codemodConstOutputAsData(tbuffer));
}

size_t ZL_TypedBuffer_numElts(const ZL_TypedBuffer* tbuffer)
{
    return STREAM_numElts(ZL_codemodConstOutputAsData(tbuffer));
}

size_t ZL_TypedBuffer_byteSize(const ZL_TypedBuffer* tbuffer)
{
    return STREAM_byteSize(ZL_codemodConstOutputAsData(tbuffer));
}

size_t ZL_TypedBuffer_eltWidth(const ZL_TypedBuffer* tbuffer)
{
    return STREAM_eltWidth(ZL_codemodConstOutputAsData(tbuffer));
}

const uint32_t* ZL_TypedBuffer_rStringLens(const ZL_TypedBuffer* tbuffer)
{
    return STREAM_rStringLens(ZL_codemodConstOutputAsData(tbuffer));
}
