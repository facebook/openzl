// Copyright (c) Meta Platforms, Inc. and affiliates.

// Design Note :
// We have multiple public headers, specialized by objects or structures,
// such as buffer, or stream.
// This design is preferred for clarity.
// If it's considered better to feature less header files,
// it would be possible to regroup multiple of them into
// some kind of generic zs2_public_types.h header.

#ifndef ZSTRONG_ZS2_DATA_H
#define ZSTRONG_ZS2_DATA_H

#include <stddef.h> // size_t
#include <stdint.h> // uint32_t

#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h" // ZL_Data

#if defined(__cplusplus)
extern "C" {
#endif

/* ================================ */
/* ======     Data types     ====== */

/**
 * Any Data object has necessary a Type.
 * The least specific Type is `ZL_Type_serial`,
 * which means it's just a blob of bytes.
 * Codecs can only accept and produce specified data Types.
 * In contrast, Selectors & Graphs may optionally accept multiple data Types,
 * using bitmap masking (example: `ZL_Type_struct | ZL_Type_numeric`).
 */
typedef enum {
    ZL_Type_serial  = 1,
    ZL_Type_struct  = 2,
    ZL_Type_numeric = 4,
    ZL_Type_string  = 8,
} ZL_Type;

// special cases
#define ZL_Type_unassigned \
    ((ZL_Type)0) /// invalid, ephemeral during transitions, but cannot be
                 /// used (read or write)
#define ZL_Type_any                                           \
    (ZL_Type)(                                                \
            ZL_Type_serial | ZL_Type_struct | ZL_Type_numeric \
            | ZL_Type_string)

#if defined(__cplusplus)
// C++ compatible version using constructor syntax
#    define ZL_DATA_ID_INPUTSTREAM (ZL_DataID{ (ZL_IDType) - 1 })
#else
// C99 compound literal
#    define ZL_DATA_ID_INPUTSTREAM (ZL_DataID){ .sid = (ZL_IDType) - 1 }
#endif

/* ============================== */
/* =====    Data object     ===== */

// Advanced parameter impacting allocation strategy, for CCtx & DCtx
typedef enum {
    ZL_DataArenaType_heap,
    ZL_DataArenaType_stack,
} ZL_DataArenaType;

// Accessors
ZL_DataID ZL_Data_id(const ZL_Data* in);

ZL_Type ZL_Data_type(const ZL_Data* data);

/**
 * @note invoking `ZL_Data_numElts()` is only valid for committed Data.
 * If the Data object was received as an input, it's necessarily valid.
 * So the issue can only happen for outputs,
 * between allocation and commit.
 * Querying `ZL_Data_numElts()` is not expected to be useful for output Data.
 * @note `ZL_Type_serial` doesn't really have a concept of "elt".
 * In this case, it returns Data size in bytes.
 */
size_t ZL_Data_numElts(const ZL_Data* data);

/**
 * @return element width in nb of bytes
 * This is only valid for fixed size elements,
 * such as `ZL_Type_struct` or `ZL_Type_numeric`.
 * If Type is `ZL_Type_string`, it returns 0 instead.
 */
size_t ZL_Data_eltWidth(const ZL_Data* data);

/**
 * @return the nb of bytes committed into data's buffer
 * (generally `== data->eltWidth * data->nbElts`).
 *
 * For `ZL_Type_string`, result is equal to `sum(data->stringLens)`.
 * Returned value is provided in nb of bytes.
 *
 * @note invoking this symbol only makes sense if Data was
 * previously committed.
 * @note (@cyan): ZS2_Data_byteSize() is another name candidate.
 */
size_t ZL_Data_contentSize(const ZL_Data* data);

/**
 * These methods provide direct access to internal buffer.
 * Warning : users must pay attention to buffer boundaries.
 * @return pointer to the _beginning_ of buffer.
 * @note for `ZL_Type_string`, returns a pointer to the buffer containing the
 * concatenated strings.
 */
const void* ZL_Data_rPtr(const ZL_Data* data);
void* ZL_Data_wPtr(ZL_Data* data);

/**
 * This method is only valid for `ZL_Type_string` Data.
 * @return a pointer to the array of string lengths.
 * The size of this array is `== ZL_Data_numElts(data)`.
 * @return `NULL` if incorrect data type, or `StringLens` not allocated yet.
 */
const uint32_t* ZL_Data_rStringLens(const ZL_Data* data);

/**
 * This method is only valid for `ZL_Type_string` Data.
 * It requires write access into StringLens array.
 * Only valid if StringLens array has already been allocated
 * and not yet written into.
 * @return NULL when any of the above conditions is violated.
 *
 * Array's capacity is supposed known from reservation request.
 * After writing into the array, the nb of Strings, which is also
 * the nb of String Lengths written, must be provided using
 * ZL_Data_commit().
 */
uint32_t* ZL_Data_wStringLens(ZL_Data* data);

/**
 * This method is only valid for `ZL_Type_string` Data.
 * It reserves memory space for StringLens array, and returns a pointer to it.
 * The buffer is owned by @p data and has the same lifetime.
 * The returned pointer can be used to write into the array.
 * After writing into the array, the nb of String Lengths provided must be
 * signaled using @ref ZL_Data_commit().
 * This method will fail if StringLens is already allocated.
 * @return `NULL` if incorrect data type, or allocation error.
 */
uint32_t* ZL_Data_reserveStringLens(ZL_Data* data, size_t nbStrings);

/**
 * @brief Commit the number of elements written into @p data.
 *
 * This method must be called exactly once for every output.
 * The @p nbElts must be `<=` reserved capacity of @p data.
 * Note that, for `ZL_Type_string`, this is the number of strings written into
 @p data.
 * The operation will automatically determine the total size of all Strings
 within @p data.
 *
 * @returns Success or an error. This function will fail if it is called more
 * than once on the same @p data, or if @p nbElts is greater than @p data's
 * capacity.

 * Terminating a Codec _without_ committing anything to @p data (not even `0`)
 * is considered an error, that is caught by the Engine
 * (classified as node processing error).
 *
 * @note @p nbElts, as "number of elements", is **not** the same as size in
 bytes written in
 * the buffer. For Numerics and Structs, the translation is
 * straighforward. For Strings, the field sizes array must be
 * provided first, using `ZL_Data_reserveStringLens()` to create
 * and access the array. The resulting useful content size will then
 * be calculated from the sum of field sizes. It will be controlled,
 * and there will be an error if sum(sizes) > bufferCapacity.
 */
ZL_Report ZL_Data_commit(ZL_Data* data, size_t nbElts);

/* =============================== */
/* =====   Data's Metadata   ===== */

/* Data's Metadata makes is possible to communicate
 * some additional information about a data's content
 * beyond the data type.
 *
 * In this first version, Metadata are open-ended,
 * So they can mean anything a producer / consumer relation needs.
 * This is intended for coordination between cooperative nodes,
 * i.e. nodes that are designed to work together.
 *
 * An example scenario is a producer (transform) adding a tag
 * which is read by a successor Selector, to guide the selection
 * process. Another example could be a fixed block size for entropy
 * split evaluation.
 *
 * Effectively, this information chanel represents a "private
 * contract" between the Data's producer and one (or more)
 * collaborative receiver(s). Receivers which are unaware of the
 * existence of the metadata will just ignore it (i.e. not even
 * request it).
 *
 * Note on code development model:
 * it's recommended to keep the code of collaborative Nodes in the
 * same unit, so that their dependence is clear, and they share
 * common definitions (enum). The "group of Nodes" can then be
 * delivered as a single "SuperNode", with method(s) that are
 * employed at Graph Construction stage "as if" it was just a
 * Node->Graph registration event.
 *
 * Note 2:
 * This capability is primarily intended to be used at compression
 * time. ZL_Data objects being also employed at decompression
 * time, the methods could be invoked during decompression too. But
 * there is no known scenario (yet) where this makes sense.
 */

/* Simple "enum"-like Medata :
 * =========================
 *
 * This is intended for some simple "tag" scenarios
 * where both @mid and @mvalue are expected to be member of some
 * `enum` list, or a relatively short range of possible values.
 */

/**
 * @brief Sets integer metadata with the key @p mId and value @p mvalue on the
 * stream.
 *
 * It is only valid to call ZL_Data_setIntMetadata() with the same @p mId
 * once. Subsequent calls with the same @p mId will return an error.
 *
 * @param mId Metdata key
 * @param mvalue Metadata value
 *
 * @returns Success or an error. This function will fail due to repeated calls
 * with the same @p mId, or upon running out of space for the metadata.
 *
 * @note In this proposed design, Int Metadata are set one by one.
 * Another possible design could follow the IntParams
 * model, where all parameters must be set all-at-once, and be
 * provided as a single vector of IntParams structures.
 *
 * @note The set value is an int, hence it's not suitable to store "large"
 * values, like 64-bit ULL.
 */
ZL_Report ZL_Data_setIntMetadata(ZL_Data* s, int mId, int mvalue);

/* ZL_Data_getIntMetadata():
 * If @mId doesn't exist, @return .isPresent == 0 .
 * In which case, .mValue should not be interpreted.
 */
typedef struct {
    int isPresent;
    int mValue;
} ZL_IntMetadata;
ZL_IntMetadata ZL_Data_getIntMetadata(const ZL_Data* s, int mId);

#if 0
/* Not implemented yet */

/* Open-ended "POD"-like Metadata :
 * ==============================
 *
 * Note : Not implemented (yet)
 *
 * This is intended for more complex coordination scenarios,
 * and is used to share any kind of POD content,
 * between a produced and collaborative consumer(s).
 * Avoid usage of pointers inside the POD content whenever possible,
 * as only the pointer will be copied, not the second-level content.
 * If there is really no other way, ensure pointed memory remains "stable".
 *
 * Allowing POD-like Metadata dramatically expands
 * the potential scope of secret contracts between collaborative nodes.
 * I'm a bit concerned by the maintenance issues that come with it,
 * hence I was not totally sure if it's a good idea to enable it.
 * But as long as collaborative nodes are coded together in the same unit,
 * this seems manageable.
 *
 * So far, most identified scenarios seem to fall into
 * the Standard Stream Features category (See below)
 */

/* Note on current interface suggested for this feature :
 * Note 1 : @mid for GenericMetadata belong to a separate plane.
 *          They can't collide with @mid for IntMetadata.
 * Note 2 : same problematic issues for setMetadata() exist,
 *          though due to the larger sizes of Generic Metadata,
 *          storage overflow is now more likely to happen.
 *          Yet, same reasoning and solution seem applicable.
 * Note 3 : if @mid doesn't exist, getGenericMetadata() @return .mPtr=NULL
 */

typedef struct {
    int mId;
    const void* mPtr;
    size_t mSize;
} ZS2_GenericMetadata;

// Note : Metadata content { mPtr, mSize } is copied internally
void ZS2_Data_setGenericMetadata(ZL_Data* s, ZS2_GenericMetadata m);

ZS2_GenericMetadata ZS2_Data_getGenericMetadata(const ZL_Data* s, int mId);

/* Standard Stream Features
 * =========================
 * This is for typical statistical information which are generally useful
 * for a large range of transforms and selectors.
 * Examples :
 * - Byte histogram (all types)
 * - min / max / range (Numeric only, interpreted as unsigned (1) or signed(2))
 *
 * There are 2 different API designs in competition :
 * - Provide a named method for each Standard Stream feature.
 *   This would be a continuation of current ZL_Data API,
 *   with methods such as ZL_Data_eltWidth() or ZL_Data_numElts().
 * - Provide one standard method, following the model of GenericMetadata,
 *   with an enum list and documentation which explains
 *   which @mid corresponds to which information.
 *
 * For clarity benefits, I tend to prefer the first design,
 * with a named method for each Standard Feature.
 *
 * Note that, previous work on the topic seems to underline
 * a need for 2 different `get` methods (per metadata):
 * - One that *tries* to read already set metadata,
 *   and eventually fails if it's not set
 *   but costs almost zero to read or fail,
 * - One that requests the information,
 *   and eventually process it inline if it's not available.
 *   This one will guarantee the availability of information,
 *   but there will be a corresponding cost to process it.
 *
 * There are also a few more topics associated :
 * - When Metadata is actively processed, it could be cached,
 *   in order to avoid paying the processing cost a second time.
 *   This is a speed optimization,
 *   but it has consequences, as caching == writing in memory,
 *   so a reference state like `const ZL_Data *` is problematic.
 *   It can be circumvented, but possibly at the cost of a more complex API.
 * - If Metadata may or may not be there, the processing time is variable.
 *   It makes speed projections difficult.
 *   A defensive strategy would be to assume there is always a processing cost.
 * - Trusting Metadata set by a Custom Transform.
 *   Can such Metadata be trusted ?
 *   What happens if it's wrong ?
 *   For example, if the Histogram is wrong, and misses some symbols,
 *   relying on this information for an encoding stage would fail badly,
 *   and integrating the possibility that the Histogram might be wrong
 *   in the encoder would complexify it, possibly making it slower.
 *   Q: Should the ability to set of Standard Stream Features be limited ?
 */

/* Example of Standard Feature : Byte Histogram ( for comments )
 *
 * This Metadata can be made available for any stream type,
 * since metrics would not change whatever the interpretation of the stream.
 *
 * If deemed preferable, it could also be restricted to
 * zs2_type_serial and token/numeric of size 1.
 * In which case, requesting the Metadata on a wrong type would @return NULL.
 *
 * The try*() variant only returns requested information if it's available.
 * Otherwise, it returns NULL.
 *
 * get*() will either retrieve existing Histogram if it already exists,
 * or process it on the fly if not.
 * Note : get*() name is up for debate.
 */

typedef struct {
    const unsigned* count;
    size_t alphabetSize; // size of count[], must be <= 256
} ZS2_ByteHistogram;
ZS2_ByteHistogram ZS2_Data_tryByteHistogram(const ZL_Data* s);
ZS2_ByteHistogram ZS2_Data_getByteHistogram(const ZL_Data* s);

/* set*() can only be employed by the Stream's generator.
 * It's up for debate if it's acceptable to trust such an input.
 * Maybe this should be reserved for internal Standard transforms only.
 */
void ZS2_Data_setByteHistogram(ZL_Data* s, ZS2_ByteHistogram h);

/* to be continued */

#endif

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_ZS2_DATA_H
