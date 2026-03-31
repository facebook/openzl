// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/a1cbor_helpers.h"

#include "openzl/common/allocation.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"

////////////////////////////////////////
// Error Handling
////////////////////////////////////////

#define A1C_ErrorType_ok__converted_code ZL_ErrorCode_no_error
#define A1C_ErrorType_badAlloc__converted_code ZL_ErrorCode_allocation
#define A1C_ErrorType_truncated__converted_code \
    ZL_ErrorCode_internalBuffer_tooSmall
#define A1C_ErrorType_invalidItemHeader__converted_code ZL_ErrorCode_corruption
#define A1C_ErrorType_largeIntegersUnsupported__converted_code \
    ZL_ErrorCode_temporaryLibraryLimitation
#define A1C_ErrorType_integerOverflow__converted_code \
    ZL_ErrorCode_integerOverflow
#define A1C_ErrorType_invalidChunkedString__converted_code \
    ZL_ErrorCode_corruption
#define A1C_ErrorType_maxDepthExceeded__converted_code \
    ZL_ErrorCode_temporaryLibraryLimitation
#define A1C_ErrorType_invalidSimpleEncoding__converted_code \
    ZL_ErrorCode_corruption
#define A1C_ErrorType_breakNotAllowed__converted_code ZL_ErrorCode_corruption
#define A1C_ErrorType_writeFailed__converted_code ZL_ErrorCode_corruption
#define A1C_ErrorType_invalidSimpleValue__converted_code ZL_ErrorCode_corruption
#define A1C_ErrorType_formatError__converted_code ZL_ErrorCode_corruption
#define A1C_ErrorType_trailingData__converted_code ZL_ErrorCode_corruption
#define A1C_ErrorType_jsonUTF8Unsupported__converted_code \
    ZL_ErrorCode_corruption

#define A1C_ERROR_TYPE_TO_ZS2_ERROR_CODE(a1c_error_type) \
    (a1c_error_type##__converted_code)

#define FOR_EACH_A1C_ERROR_TYPE(func)             \
    func(A1C_ErrorType_ok);                       \
    func(A1C_ErrorType_badAlloc);                 \
    func(A1C_ErrorType_truncated);                \
    func(A1C_ErrorType_invalidItemHeader);        \
    func(A1C_ErrorType_largeIntegersUnsupported); \
    func(A1C_ErrorType_integerOverflow);          \
    func(A1C_ErrorType_invalidChunkedString);     \
    func(A1C_ErrorType_maxDepthExceeded);         \
    func(A1C_ErrorType_invalidSimpleEncoding);    \
    func(A1C_ErrorType_breakNotAllowed);          \
    func(A1C_ErrorType_writeFailed);              \
    func(A1C_ErrorType_invalidSimpleValue);       \
    func(A1C_ErrorType_formatError);              \
    func(A1C_ErrorType_trailingData);             \
    func(A1C_ErrorType_jsonUTF8Unsupported)

#define A1C_ERROR_CASE_TO_CONVERT_CODE(a1c_error_type) \
    case a1c_error_type:                               \
        return A1C_ERROR_TYPE_TO_ZS2_ERROR_CODE(a1c_error_type)

static ZL_ErrorCode A1C_Error_convertCode(A1C_ErrorType type)
{
    switch (type) {
        FOR_EACH_A1C_ERROR_TYPE(A1C_ERROR_CASE_TO_CONVERT_CODE);
        default:
            ZL_ASSERT_FAIL("Unreachable!");
            return ZL_ErrorCode_logicError;
    }
}

#define A1C_ERROR_CASE_TO_DECLARE_STATIC_ERROR_INFO(a1c_error_type)                \
    case a1c_error_type: {                                                         \
        ZL_E_DECLARE_STATIC_ERROR_INFO(                                            \
                _static_error_info,                                                \
                A1C_ERROR_TYPE_TO_ZS2_ERROR_CODE(a1c_error_type),                  \
                "Encountered error in A1CBOR library with code \"" #a1c_error_type \
                "\".");                                                            \
        return ZL_E_POINTER_TO_STATIC_ERROR_INFO(_static_error_info);              \
    }

static const ZL_StaticErrorInfo* A1C_Error_getStaticErrorInfo(
        A1C_ErrorType type)
{
    switch (type) {
        FOR_EACH_A1C_ERROR_TYPE(A1C_ERROR_CASE_TO_DECLARE_STATIC_ERROR_INFO);
        default:
            ZL_ASSERT_FAIL("Unreachable!");
            return NULL;
    }
}

ZL_Error A1C_Error_convert(
        const ZL_ErrorContext* const error_context,
        A1C_Error a1c_err)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(error_context->opCtx);
    if (a1c_err.type == A1C_ErrorType_ok) {
        return ZL_E_EMPTY;
    }
    ZL_Error zs_err = ZL_E_create(
            A1C_Error_getStaticErrorInfo(a1c_err.type),
            error_context,
            a1c_err.file,
            NULL,
            a1c_err.line,
            A1C_Error_convertCode(a1c_err.type),
            "Encountered error in A1CBOR library with code \"%s\".",
            A1C_ErrorType_getString(a1c_err.type));
    zs_err = ZL_E_ADDFRAME(zs_err, ZL_EE_EMPTY, "");
    return zs_err;
}

////////////////////////////////////////
// Arena
////////////////////////////////////////

static void* wrapped_arena_calloc(void* const opaque, const size_t bytes)
{
    Arena* const inner_arena = (Arena*)opaque;
    return ALLOC_Arena_calloc(inner_arena, bytes);
}

A1C_Arena A1C_Arena_wrap(Arena* const inner_arena)
{
    A1C_Arena arena;
    arena.calloc = wrapped_arena_calloc;
    arena.opaque = inner_arena;
    return arena;
}

////////////////////////////////////////
// Conversion
////////////////////////////////////////

ZL_Report A1C_convert_cbor_to_json(
        ZL_ErrorContext* const error_context,
        Arena* const arena,
        void** const dst,
        size_t* const dstSize,
        const StringView cbor)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(error_context);

    ZL_ERR_IF_NULL(arena, parameter_invalid);
    ZL_ERR_IF_NULL(dst, parameter_invalid);
    ZL_ERR_IF_NULL(dstSize, parameter_invalid);
    ZL_ERR_IF_NULL(cbor.data, parameter_invalid);

    const A1C_Arena a1c_arena              = A1C_Arena_wrap(arena);
    const A1C_DecoderConfig decoder_config = (A1C_DecoderConfig){
        .maxDepth            = 0,
        .limitBytes          = 0,
        .referenceSource     = true,
        .rejectUnknownSimple = true,
    };
    A1C_Decoder decoder;
    A1C_Decoder_init(&decoder, a1c_arena, decoder_config);

    const A1C_Item* const root =
            A1C_Decoder_decode(&decoder, (const uint8_t*)cbor.data, cbor.size);
    if (root == NULL) {
        return ZL_WRAP_ERROR(A1C_Error_convert(
                ZL_ERR_CTX_PTR, A1C_Decoder_getError(&decoder)));
    }

    const size_t encoded_size = A1C_Item_jsonSize(root);
    const size_t alloc_size   = encoded_size + 1; // space for null terminator

    uint8_t* buf;
    bool alloced;
    if (*dst != NULL && *dstSize >= alloc_size) {
        alloced = false;
        buf     = *dst;
    } else {
        alloced = true;
        buf     = ALLOC_Arena_malloc(arena, alloc_size);
    }
    ZL_ERR_IF_NULL(buf, allocation);

    A1C_Error error;
    const size_t written = A1C_Item_json(root, buf, encoded_size, &error);
    if (written == 0) {
        if (alloced) {
            ALLOC_Arena_free(arena, buf);
        }
        return ZL_WRAP_ERROR(A1C_Error_convert(ZL_ERR_CTX_PTR, error));
    }

    ZL_ERR_IF_NE(
            written,
            encoded_size,
            GENERIC,
            "Serialized size (%lu) didn't end up being the size we expected (%lu).",
            written,
            encoded_size);

    buf[encoded_size] = '\0';

    *dst     = buf;
    *dstSize = encoded_size;

    return ZL_returnSuccess();
}
