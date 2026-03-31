// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_TRANSFORMS_FLOAT_DECONSTRUCT_COMMON_FLOAT_DECONSTRUCT_BINDING_H
#define ZSTRONG_TRANSFORMS_FLOAT_DECONSTRUCT_COMMON_FLOAT_DECONSTRUCT_BINDING_H

#include "openzl/common/errors_internal.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

/* The float deconstruct transform currently supports float32, bfloat16, and
 * float16 element types. This enum is sent as a transform header, to
 * indicate the element type to the decoder. */
typedef enum {
    FLTDECON_ElementType_float32  = 0,
    FLTDECON_ElementType_bfloat16 = 1,
    FLTDECON_ElementType_float16  = 2,
} FLTDECON_ElementType_e;

static const FLTDECON_ElementType_e FLTDECON_ElementTypeEnumMaxValue =
        FLTDECON_ElementType_float16;

ZL_INLINE_KEYWORD ZL_Report FLTDECON_ElementWidth(FLTDECON_ElementType_e type)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT((ZL_OperationContext*)NULL);
    switch (type) {
        case FLTDECON_ElementType_float32:
            return ZL_returnValue(4);
        case FLTDECON_ElementType_bfloat16:
            return ZL_returnValue(2);
        case FLTDECON_ElementType_float16:
            return ZL_returnValue(2);
    }
    ZL_ERR(logicError);
}

ZL_INLINE_KEYWORD ZL_Report FLTDECON_SignFracWidth(FLTDECON_ElementType_e type)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT((ZL_OperationContext*)NULL);
    switch (type) {
        case FLTDECON_ElementType_float32:
            return ZL_returnValue(3);
        case FLTDECON_ElementType_bfloat16:
            return ZL_returnValue(1);
        case FLTDECON_ElementType_float16:
            return ZL_returnValue(2);
    }
    ZL_ERR(logicError);
}

ZL_INLINE_KEYWORD ZL_Report FLTDECON_ExponentWidth(FLTDECON_ElementType_e type)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT((ZL_OperationContext*)NULL);
    ZL_ERR_IF_GT(type, FLTDECON_ElementTypeEnumMaxValue, logicError);
    return ZL_returnValue(1);
}

ZL_END_C_DECLS

#endif
