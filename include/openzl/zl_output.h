// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_ZS2_OUTPUT_H
#define ZSTRONG_ZS2_OUTPUT_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_portability.h"

#if defined(__cplusplus)
extern "C" {
#endif

// TODO: Delete these codemod shims once unused
ZL_INLINE ZL_Data* ZL_codemodOutputAsData(ZL_Output* output)
{
    void* ptr = output;
    return (ZL_Data*)ptr;
}
ZL_INLINE ZL_Output* ZL_codemodDataAsOutput(ZL_Data* data)
{
    void* ptr = data;
    return (ZL_Output*)ptr;
}
ZL_INLINE const ZL_Data* ZL_codemodConstOutputAsData(const ZL_Output* output)
{
    const void* ptr = output;
    return (const ZL_Data*)ptr;
}
ZL_INLINE const ZL_Output* ZL_codemodConstDataAsOutput(const ZL_Data* data)
{
    const void* ptr = data;
    return (const ZL_Output*)ptr;
}
ZL_INLINE const ZL_Output** ZL_codemodConstDatasAsOutputs(const ZL_Data** datas)
{
    void* ptr = datas;
    return (const ZL_Output**)ptr;
}
ZL_INLINE ZL_Data** ZL_codemodOutputsAsDatas(ZL_Output** outputs)
{
    void* ptr = outputs;
    return (ZL_Data**)ptr;
}

ZL_INLINE ZL_Type ZL_Output_type(const ZL_Output* output)
{
    return ZL_Data_type(ZL_codemodConstOutputAsData(output));
}

ZL_INLINE ZL_DataID ZL_Output_id(const ZL_Output* output)
{
    return ZL_Data_id(ZL_codemodConstOutputAsData(output));
}

/**
 * @returns The element width of the @p output if it has a buffer reserved,
 * otherwise it returns NULL. Within a custom codec, this function always
 * succeeds, because the output always has a buffer reserved. If the type
 * of the output is String, it returns 0 instead.
 */
ZL_Report ZL_Output_eltWidth(const ZL_Output* output);

/**
 * @returns The number of elements committed in @p output if it has been
 * committed. Otherwise, returns an error if the write to @p output has not been
 * committed.
 */
ZL_Report ZL_Output_numElts(const ZL_Output* output);

/**
 * @returns The content size in bytes that has been committed to @p output.
 * For non-string types, this is the eltWidth * numElts. For string types, this
 * is the sum of the lengths of each stream. If @p output has not been
 * committed, it returns an error.
 */
ZL_Report ZL_Output_contentSize(const ZL_Output* output);

/**
 * @returns The capacity of the buffer reserved for @p output in number of
 * elements. If @p output has not been reserved, it returns an error.
 * For string types, this is the number of strings that can be written into the
 * buffer.
 */
ZL_Report ZL_Output_eltsCapacity(const ZL_Output* output);

/**
 * @returns The capacity of the buffer reserved for @p output in bytes. If
 * @p output has not been reserved, it returns an error. For string types, this
 * is the sum of the lengths of each string that can be written into the buffer.
 */
ZL_Report ZL_Output_contentCapacity(const ZL_Output* output);

/**
 * These methods provide direct access to internal buffer.
 * Warning : users must pay attention to buffer boundaries.
 * @return pointer to buffer position to resume writing.
 * @note for `ZL_Type_string`, returns a pointer to the buffer containing the
 * concatenated strings.
 */
ZL_INLINE void* ZL_Output_ptr(ZL_Output* output)
{
    return ZL_Data_wPtr(ZL_codemodOutputAsData(output));
}

/**
 * @returns a const pointer to the _beginning_ of the buffer.
 * It returns NULL if the output does not have a buffer attached to it yet.
 * This cannot happen within a custom codec.
 * Warning : users must pay attention to buffer boundaries.
 * @note for `ZL_Type_string`, returns a pointer to the buffer containing the
 * concatenated strings.
 */
ZL_INLINE const void* ZL_Output_constPtr(const ZL_Output* output)
{
    return ZL_Data_rPtr(ZL_codemodConstOutputAsData(output));
}

/**
 * This method is only valid for `ZL_Type_string` Data.
 * It requests write access into StringLens array.
 * Only valid if StringLens array has already been allocated.
 * @return pointer to array position to resume writing.
 * or NULL if any of above conditions is violated.
 *
 * Array's capacity is supposed known from reservation request.
 * After writing into the array, the nb of Strings, which is also
 * the nb of String Lengths written, must be provided using
 * ZL_Data_commit().
 */
ZL_INLINE uint32_t* ZL_Output_stringLens(ZL_Output* output)
{
    return ZL_Data_wStringLens(ZL_codemodOutputAsData(output));
}

/**
 * @returns A const pointer to the array containing lengths for string-typed
 * outputs. It returns NULL if the output is not of type string.
 */
ZL_INLINE const uint32_t* ZL_Output_constStringLens(const ZL_Output* output)
{
    return ZL_Data_rStringLens(ZL_codemodConstOutputAsData(output));
}

/**
 * This method is only valid for `ZL_Type_string` Data.
 * It reserves memory space for StringLens array, and returns a pointer to it.
 * The buffer is owned by @p data and has the same lifetime.
 * The returned pointer can be used to write into the array.
 * After writing into the array, the nb of String Lengths provided must be
 * signaled using @ref ZL_Output_commit().
 * This method will fail if StringLens is already allocated.
 * @return `NULL` if incorrect data type, or allocation error.
 */
ZL_INLINE uint32_t* ZL_Output_reserveStringLens(
        ZL_Output* output,
        size_t numStrings)
{
    return ZL_Data_reserveStringLens(
            ZL_codemodOutputAsData(output), numStrings);
}

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
ZL_INLINE ZL_Report ZL_Output_commit(ZL_Output* output, size_t numElts)
{
    return ZL_Data_commit(ZL_codemodOutputAsData(output), numElts);
}

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

/* Simple "enum"-like Metadata :
 * =========================
 *
 * This is intended for some simple "tag" scenarios
 * where both @key and @value are expected to be member of some
 * `enum` list, or a relatively short range of possible values.
 */

/**
 * @brief Sets integer metadata with the key @p key and value @p value on the
 * stream.
 *
 * It is only valid to call ZL_Data_setIntMetadata() with the same @p key
 * once. Subsequent calls with the same @p key will return an error.
 *
 * @param key Metdata key
 * @param value Metadata value
 *
 * @returns Success or an error. This function will fail due to repeated calls
 * with the same @p key, or upon running out of space for the metadata.
 *
 * @note In this proposed design, Int Metadata are set one by one.
 * Another possible design could follow the IntParams
 * model, where all parameters must be set all-at-once, and be
 * provided as a single vector of IntParams structures.
 *
 * @note The set value is an int, hence it's not suitable to store "large"
 * values, like 64-bit ULL.
 */
ZL_INLINE ZL_Report
ZL_Output_setIntMetadata(ZL_Output* output, int key, int value)
{
    return ZL_Data_setIntMetadata(ZL_codemodOutputAsData(output), key, value);
}

/**
 * @returns The value if present. ZL_IntMetadata::isPresent != 0
 * when the @p key exists, in which case ZL_IntMetadata::mValue is set to the
 * value.
 */
ZL_INLINE ZL_IntMetadata
ZL_Output_getIntMetadata(const ZL_Output* output, int key)
{
    return ZL_Data_getIntMetadata(ZL_codemodConstOutputAsData(output), key);
}

#if defined(__cplusplus)
}
#endif

#endif
