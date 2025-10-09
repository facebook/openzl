// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_ZL_INPUT_H
#define ZSTRONG_ZL_INPUT_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/zl_data.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_portability.h"

#if defined(__cplusplus)
extern "C" {
#endif

// TODO: Delete these codemod shims once unused
ZL_INLINE const ZL_Data* ZL_codemodInputAsData(const ZL_Input* input)
{
    const void* ptr = input;
    return (const ZL_Data*)ptr;
}
ZL_INLINE const ZL_Input* ZL_codemodDataAsInput(const ZL_Data* data)
{
    const void* ptr = data;
    return (const ZL_Input*)ptr;
}
ZL_INLINE ZL_Data* ZL_codemodMutInputAsData(ZL_Input* input)
{
    void* ptr = input;
    return (ZL_Data*)ptr;
}
ZL_INLINE ZL_Input* ZL_codemodMutDataAsInput(ZL_Data* data)
{
    void* ptr = data;
    return (ZL_Input*)ptr;
}
ZL_INLINE const ZL_Input** ZL_codemodDatasAsInputs(const ZL_Data** datas)
{
    void* ptr = datas;
    return (const ZL_Input**)ptr;
}
ZL_INLINE const ZL_Data** ZL_codemodInputsAsDatas(const ZL_Input** inputs)
{
    void* ptr = inputs;
    return (const ZL_Data**)ptr;
}

ZL_INLINE ZL_DataID ZL_Input_id(const ZL_Input* input)
{
    return ZL_Data_id(ZL_codemodInputAsData(input));
}

ZL_INLINE ZL_Type ZL_Input_type(const ZL_Input* input)
{
    return ZL_Data_type(ZL_codemodInputAsData(input));
}

/**
 * @note invoking `ZL_Data_numElts()` is only valid for committed Data.
 * If the Data object was received as an input, it's necessarily valid.
 * So the issue can only happen for outputs,
 * between allocation and commit.
 * Querying `ZL_Data_numElts()` is not expected to be useful for output Data.
 * @note `ZL_Type_serial` doesn't really have a concept of "elt".
 * In this case, it returns Data size in bytes.
 */
ZL_INLINE size_t ZL_Input_numElts(const ZL_Input* input)
{
    return ZL_Data_numElts(ZL_codemodInputAsData(input));
}

/**
 * @return element width in nb of bytes
 * This is only valid for fixed size elements,
 * such as `ZL_Type_struct` or `ZL_Type_numeric`.
 * If Type is `ZL_Type_string`, it returns 0 instead.
 */
ZL_INLINE size_t ZL_Input_eltWidth(const ZL_Input* input)
{
    return ZL_Data_eltWidth(ZL_codemodInputAsData(input));
}

/**
 * @return the nb of bytes committed into data's buffer
 * (generally `== data->eltWidth * data->nbElts`).
 *
 * For `ZL_Type_string`, result is equal to `sum(data->stringLens)`.
 * Returned value is provided in nb of bytes.
 *
 * @note invoking this symbol only makes sense if Data was
 * previously committed.
 * @note (@cyan): ZL_Data_byteSize() is another name candidate.
 */
ZL_INLINE size_t ZL_Input_contentSize(const ZL_Input* input)
{
    return ZL_Data_contentSize(ZL_codemodInputAsData(input));
}

/**
 * These methods provide direct access to internal buffer.
 * Warning : users must pay attention to buffer boundaries.
 * @return pointer to the _beginning_ of buffer.
 * @note for `ZL_Type_string`, returns a pointer to the buffer containing the
 * concatenated strings.
 */
ZL_INLINE const void* ZL_Input_ptr(const ZL_Input* input)
{
    return ZL_Data_rPtr(ZL_codemodInputAsData(input));
}

/**
 * This method is only valid for `ZL_Type_string` Data.
 * @return a pointer to the array of string lengths.
 * The size of this array is `== ZL_Data_numElts(data)`.
 * @return `NULL` if incorrect data type, or `StringLens` not allocated yet.
 */
ZL_INLINE const uint32_t* ZL_Input_stringLens(const ZL_Input* input)
{
    return ZL_Data_rStringLens(ZL_codemodInputAsData(input));
}

/**
 * @returns The value if present. ZL_IntMetadata::isPresent != 0
 * when the @p key exists, in which case ZL_IntMetadata::mValue is set to the
 * value.
 */
ZL_INLINE ZL_IntMetadata ZL_Input_getIntMetadata(const ZL_Input* input, int key)
{
    return ZL_Data_getIntMetadata(ZL_codemodInputAsData(input), key);
}

/**
 * @brief Sets integer metadata with the key @p key and value @p value on the
 * stream.
 *
 * It is only valid to call ZL_Input_setIntMetadata() with the same @p key
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
ZL_INLINE ZL_Report ZL_Input_setIntMetadata(ZL_Input* input, int key, int value)
{
    return ZL_Data_setIntMetadata(ZL_codemodMutInputAsData(input), key, value);
}

#if defined(__cplusplus)
}
#endif

#endif
