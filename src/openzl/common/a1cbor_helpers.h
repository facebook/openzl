// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_A1CBOR_HELPERS_H
#define ZSTRONG_A1CBOR_HELPERS_H

#include "openzl/zl_errors.h"
#include "openzl/zl_macro_helpers.h"

#include "openzl/common/allocation.h"

#include "openzl/shared/a1cbor.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/string_view.h"

ZL_BEGIN_C_DECLS

ZL_Error A1C_Error_convert(
        const ZL_ErrorContext* error_context,
        A1C_Error error);

A1C_Arena A1C_Arena_wrap(Arena* inner_arena);

/**
 * Converts a CBOR struct to a JSON equivalent. The written JSON is NULL-
 * terminated, although that '\0' byte is not included in the length written
 * into @p dstSize.
 *
 * This function uses @p dst and @dstSize both as (1) input arguments that
 * optionally indicate an existing buffer into which the output of the
 * serialization process can be placed as well as (2) output arguments
 * indicating where the output actually was placed.
 *
 * When @p dst points to a `void*` variable with a non-`NULL` initial value,
 * and @p dstSize points to a `size_t` variable with a non-zero initial value,
 * this function will attempt to write the serialized output into the buffer
 * pointed to by `*dst` with capacity `*dstSize`. If the output fits in that
 * provided buffer, then `*dst` will be left unchanged, and `*dstSize` will be
 * updated to reflect the written size of the output.
 *
 * Otherwise, either because the output doesn't fit in the provided buffer or
 * because no buffer was provided (`*dst` is `NULL` or `*dstSize == 0`), an
 * output buffer of sufficient size to hold the output is allocated. `*dst` is
 * set to point to the start of that buffer and `*dstSize` is set to the size
 * of the output. That buffer is owned by @p arena and will be freed when the
 * @p arena is destroyed.
 *
 * @param[in out] dst     Pointer to a variable pointing to the output buffer,
 *                        which can start out either pointing to an existing
 *                        output buffer or `NULL`. That variable will be set to
 *                        point to the output buffer actually used.
 * @param[in out] dstSize Pointer to a variable that should be initialized to
 *                        the capacity of the output buffer, if one is being
 *                        provided, or 0 otherwise. That variable will be set
 *                        to contain the written size of the output.
 *
 * @returns success or an error.
 */
ZL_Report A1C_convert_cbor_to_json(
        ZL_ErrorContext* error_context,
        Arena* arena,
        void** dst,
        size_t* dstSize,
        StringView cbor);

/**
 * Trivial helper to wrap an `A1C_String` into a `StringView`.
 *
 * Note: `A1C_String`s are not null terminated! This `StringView` won't be,
 * either!
 */
ZL_INLINE StringView StringView_initFromA1C(const A1C_String str)
{
    return StringView_init(str.data, str.size);
}

ZL_INLINE void A1C_Item_string_refStringView(
        A1C_Item* item,
        const StringView sv)
{
    A1C_Item_string_ref(item, sv.data, sv.size);
}

typedef A1C_Pair* A1C_PairPtr;
ZL_RESULT_DECLARE_TYPE(A1C_PairPtr);

/**
 * Helper function to try adding an item using a map builder. Converts the
 * failure paths to Zstrong errors. Mostly intended to be an implementation
 * detail of @ref A1C_MAP_TRY_ADD_R(), which is the easy way to consume the
 * returned result.
 */
ZL_INLINE ZL_RESULT_OF(A1C_PairPtr)
        A1C_MapBuilder_tryAdd(const A1C_MapBuilder builder)
{
    A1C_Pair* const pair = A1C_MapBuilder_add(builder);
    if (pair == NULL) {
        if (builder.map == NULL) {
            ZL_RET_T_IF_NULL(A1C_PairPtr, allocation, pair);
        } else {
            ZL_RET_T_IF_NULL(A1C_PairPtr, GENERIC, pair);
        }
    }
    return ZL_RESULT_WRAP_VALUE(A1C_PairPtr, pair);
}

typedef A1C_Item* A1C_ItemPtr;
ZL_RESULT_DECLARE_TYPE(A1C_ItemPtr);

/**
 * Helper function to try adding an item using an array builder. Converts the
 * failure paths to Zstrong errors. Mostly intended to be an implementation
 * detail of @ref A1C_ARRAY_TRY_ADD_R(), which is the easy way to consume the
 * returned result.
 */
ZL_INLINE ZL_RESULT_OF(A1C_ItemPtr)
        A1C_ArrayBuilder_tryAdd(const A1C_ArrayBuilder builder)
{
    A1C_Item* const item = A1C_ArrayBuilder_add(builder);
    if (item == NULL) {
        if (builder.array == NULL) {
            ZL_RET_T_IF_NULL(A1C_ItemPtr, allocation, item);
        } else {
            ZL_RET_T_IF_NULL(A1C_ItemPtr, GENERIC, item);
        }
    }
    return ZL_RESULT_WRAP_VALUE(A1C_ItemPtr, item);
}

/**
 * Helper macros to progressively build collections. E.g.:
 *
 * ```
 * ZL_RESULT_OF(Foo) some_function(A1C_Item* item) {
 *   ZL_RESULT_DECLARE_SCOPE(Foo, ...);
 *   const A1C_MapBuilder builder = A1C_Item_map_builder(item, 4, arena);
 *   {
 *     A1C_MAP_TRY_ADD(pair, builder);
 *     A1C_Item_string_refCStr(&pair->key, "key1");
 *     A1C_Item_int64(&pair->value, 1);
 *   }
 *   {
 *     A1C_MAP_TRY_ADD(pair, builder);
 *     A1C_Item_string_refCStr(&pair->key, "key2");
 *     A1C_Item_int64(&pair->value, 2);
 *   }
 *   return ZL_returnSuccess();
 * }
 * ```
 *
 * This will build a map of size 2.
 */

#define A1C_MAP_TRY_ADD(_var, _builder) \
    ZL_TRY_LET_CONST(A1C_PairPtr, _var, A1C_MapBuilder_tryAdd(_builder));
#define A1C_ARRAY_TRY_ADD(_var, _builder) \
    ZL_TRY_LET_CONST(A1C_ItemPtr, _var, A1C_ArrayBuilder_tryAdd(_builder));

/**
 * Deprecated versions using the old error type-passing pattern.
 */

#define A1C_MAP_TRY_ADD_R(_var, _builder) \
    A1C_MAP_TRY_ADD_T(size_t, _var, _builder)
#define A1C_ARRAY_TRY_ADD_R(_var, _builder) \
    A1C_ARRAY_TRY_ADD_T(size_t, _var, _builder)

#define A1C_MAP_TRY_ADD_T(_return_type, _var, _builder) \
    ZL_TRY_LET_CONST_TT(                                \
            _return_type, A1C_PairPtr, _var, A1C_MapBuilder_tryAdd(_builder));
#define A1C_ARRAY_TRY_ADD_T(_return_type, _var, _builder) \
    ZL_TRY_LET_CONST_TT(                                  \
            _return_type,                                 \
            A1C_ItemPtr,                                  \
            _var,                                         \
            A1C_ArrayBuilder_tryAdd(_builder));

/**
 * Helper macros to instantiate a new (const) variable with the contents of an
 * A1C item, assuming that it is of the specified type. Otherwise, returns an
 * error. E.g.:
 *
 * ```
 * ZL_RESULT(Foo) some_function() {
 *   ZL_RESULT_DECLARE_SCOPE(Foo, ...);
 *   // ...
 *   const A1C_Item* item = ...;
 *   A1C_TRY_EXTRACT_MAP(map, item);
 *
 *   for (size_t i = 0; i < map.size; i++) {
 *     // ...
 *   }
 * }
 * ```
 */

#define A1C_TRY_EXTRACT_BOOL(_var, _expr) A1C_TRY_EXTRACT(Bool, _var, _expr)
#define A1C_TRY_EXTRACT_INT64(_var, _expr) A1C_TRY_EXTRACT(Int64, _var, _expr)
#define A1C_TRY_EXTRACT_FLOAT16(_var, _expr) \
    A1C_TRY_EXTRACT(Float16, _var, _expr)
#define A1C_TRY_EXTRACT_FLOAT32(_var, _expr) \
    A1C_TRY_EXTRACT(Float32, _var, _expr)
#define A1C_TRY_EXTRACT_FLOAT64(_var, _expr) \
    A1C_TRY_EXTRACT(Float64, _var, _expr)
#define A1C_TRY_EXTRACT_BYTES(_var, _expr) A1C_TRY_EXTRACT(Bytes, _var, _expr)
#define A1C_TRY_EXTRACT_STRING(_var, _expr) A1C_TRY_EXTRACT(String, _var, _expr)
#define A1C_TRY_EXTRACT_MAP(_var, _expr) A1C_TRY_EXTRACT(Map, _var, _expr)
#define A1C_TRY_EXTRACT_ARRAY(_var, _expr) A1C_TRY_EXTRACT(Array, _var, _expr)
#define A1C_TRY_EXTRACT_SIMPLE(_var, _expr) A1C_TRY_EXTRACT(Simple, _var, _expr)
#define A1C_TRY_EXTRACT_TAG(_var, _expr) A1C_TRY_EXTRACT(Tag, _var, _expr)

#define A1C_TRY_EXTRACT_T_BOOL(_return_type, _var, _expr) \
    A1C_TRY_EXTRACT_TT(_return_type, Bool, _var, _expr)
#define A1C_TRY_EXTRACT_T_INT64(_return_type, _var, _expr) \
    A1C_TRY_EXTRACT_TT(_return_type, Int64, _var, _expr)
#define A1C_TRY_EXTRACT_T_FLOAT16(_return_type, _var, _expr) \
    A1C_TRY_EXTRACT_TT(_return_type, Float16, _var, _expr)
#define A1C_TRY_EXTRACT_T_FLOAT32(_return_type, _var, _expr) \
    A1C_TRY_EXTRACT_TT(_return_type, Float32, _var, _expr)
#define A1C_TRY_EXTRACT_T_FLOAT64(_return_type, _var, _expr) \
    A1C_TRY_EXTRACT_TT(_return_type, Float64, _var, _expr)
#define A1C_TRY_EXTRACT_T_BYTES(_return_type, _var, _expr) \
    A1C_TRY_EXTRACT_TT(_return_type, Bytes, _var, _expr)
#define A1C_TRY_EXTRACT_T_STRING(_return_type, _var, _expr) \
    A1C_TRY_EXTRACT_TT(_return_type, String, _var, _expr)
#define A1C_TRY_EXTRACT_T_MAP(_return_type, _var, _expr) \
    A1C_TRY_EXTRACT_TT(_return_type, Map, _var, _expr)
#define A1C_TRY_EXTRACT_T_ARRAY(_return_type, _var, _expr) \
    A1C_TRY_EXTRACT_TT(_return_type, Array, _var, _expr)
#define A1C_TRY_EXTRACT_T_SIMPLE(_return_type, _var, _expr) \
    A1C_TRY_EXTRACT_TT(_return_type, Simple, _var, _expr)
#define A1C_TRY_EXTRACT_T_TAG(_return_type, _var, _expr) \
    A1C_TRY_EXTRACT_TT(_return_type, Tag, _var, _expr)

/**
 * Specializations of the above for when the enclosing scope has a return type
 * of `ZL_Report`.
 */

#define A1C_TRY_EXTRACT_R_BOOL(_var, _expr) \
    A1C_TRY_EXTRACT_T_BOOL(size_t, _var, _expr)
#define A1C_TRY_EXTRACT_R_INT64(_var, _expr) \
    A1C_TRY_EXTRACT_T_INT64(size_t, _var, _expr)
#define A1C_TRY_EXTRACT_R_FLOAT16(_var, _expr) \
    A1C_TRY_EXTRACT_T_FLOAT16(size_t, _var, _expr)
#define A1C_TRY_EXTRACT_R_FLOAT32(_var, _expr) \
    A1C_TRY_EXTRACT_T_FLOAT32(size_t, _var, _expr)
#define A1C_TRY_EXTRACT_R_FLOAT64(_var, _expr) \
    A1C_TRY_EXTRACT_T_FLOAT64(size_t, _var, _expr)
#define A1C_TRY_EXTRACT_R_BYTES(_var, _expr) \
    A1C_TRY_EXTRACT_T_BYTES(size_t, _var, _expr)
#define A1C_TRY_EXTRACT_R_STRING(_var, _expr) \
    A1C_TRY_EXTRACT_T_STRING(size_t, _var, _expr)
#define A1C_TRY_EXTRACT_R_MAP(_var, _expr) \
    A1C_TRY_EXTRACT_T_MAP(size_t, _var, _expr)
#define A1C_TRY_EXTRACT_R_ARRAY(_var, _expr) \
    A1C_TRY_EXTRACT_T_ARRAY(size_t, _var, _expr)
#define A1C_TRY_EXTRACT_R_SIMPLE(_var, _expr) \
    A1C_TRY_EXTRACT_T_SIMPLE(size_t, _var, _expr)
#define A1C_TRY_EXTRACT_R_TAG(_var, _expr) \
    A1C_TRY_EXTRACT_T_TAG(size_t, _var, _expr)

#define A1C_DECLARE_TRY_GET_T(_type, _member)        \
    A1C_DECLARE_TRY_GET_T_INNER(                     \
            ZS_MACRO_CONCAT(A1C_Item_tryGet, _type), \
            ZS_MACRO_CONCAT(A1C_, _type),            \
            ZS_MACRO_CONCAT(A1C_ItemType_, _member), \
            _member)
#define A1C_DECLARE_TRY_GET_T_INNER(                                     \
        _name, _item_type, _enum_type, _union_member)                    \
    ZL_RESULT_DECLARE_TYPE(_item_type);                                  \
    ZL_INLINE ZL_RESULT_OF(_item_type) _name(const A1C_Item* const item) \
    {                                                                    \
        ZL_RET_T_IF_NULL(_item_type, corruption, item);                  \
        ZL_RET_T_IF_NE(_item_type, corruption, item->type, _enum_type);  \
        return ZL_RESULT_WRAP_VALUE(_item_type, item->_union_member);    \
    }

/**
 * These macro invocations declare a family of types and functions, one for
 * each `A1C_ItemType`, like, e.g.,
 *
 * ```
 * ZL_RESULT_DECLARE_TYPE(A1C_Map);
 * ZL_RESULT_OF(A1C_Map) A1C_Item_tryGetMap(const A1C_Item* item);
 * ```
 *
 * You can use these as helpers to extract the value of A1C nodes, when you
 * expect them to be a certain type.
 */

A1C_DECLARE_TRY_GET_T(Bool, boolean)
A1C_DECLARE_TRY_GET_T(Int64, int64)
A1C_DECLARE_TRY_GET_T(Float16, float16)
A1C_DECLARE_TRY_GET_T(Float32, float32)
A1C_DECLARE_TRY_GET_T(Float64, float64)
A1C_DECLARE_TRY_GET_T(Bytes, bytes)
A1C_DECLARE_TRY_GET_T(String, string)
A1C_DECLARE_TRY_GET_T(Map, map)
A1C_DECLARE_TRY_GET_T(Array, array)
A1C_DECLARE_TRY_GET_T(Simple, simple)
A1C_DECLARE_TRY_GET_T(Tag, tag)

#undef A1C_DECLARE_TRY_GET_T_INNER
#undef A1C_DECLARE_TRY_GET_T

////////////////////////////////////////
// Implementation Details
////////////////////////////////////////

#define A1C_TRY_EXTRACT(_a1c_type_suffix, _var, _expr) \
    ZL_TRY_LET_CONST(                                  \
            ZS_MACRO_CONCAT(A1C_, _a1c_type_suffix),   \
            _var,                                      \
            ZS_MACRO_CONCAT(A1C_Item_tryGet, _a1c_type_suffix)((_expr)))

#define A1C_TRY_EXTRACT_T(_a1c_type_suffix, _var, _expr) \
    A1C_TRY_EXTRACT_TT(size_t, _a1c_type_suffix, _var, _expr)

#define A1C_TRY_EXTRACT_TT(_return_type, _a1c_type_suffix, _var, _expr) \
    ZL_TRY_LET_CONST_TT(                                                \
            _return_type,                                               \
            ZS_MACRO_CONCAT(A1C_, _a1c_type_suffix),                    \
            _var,                                                       \
            ZS_MACRO_CONCAT(A1C_Item_tryGet, _a1c_type_suffix)((_expr)))

ZL_END_C_DECLS

#endif // ZSTRONG_A1CBOR_HELPERS_H
