// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <string.h>

#include "common_gcd.h" // ZL_gcdVec
#include "encode_divide_by_binding.h"
#include "encode_divide_by_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/varint.h" // ZL_varintEncode64Fast
#include "openzl/zl_public_nodes.h"

/*
 * This function returns the divisor to use for the divide by transform.
 * @p divisor is used as the divisor if it is not 0. Otherwise, the GCD of the
 * array is used as the divisor. Also ensures that the divisor is valid, both
 * that it is not 0 and that all elements in the array are divisible by it.
 */
static ZL_RESULT_OF(uint64_t) getDivisor(
        size_t intWidth,
        size_t nbInts,
        uint64_t divisor,
        const void* src)
{
    if (divisor == 0) {
        divisor = ZL_gcdVec(src, nbInts, intWidth);
        return ZL_RESULT_WRAP_VALUE(uint64_t, divisor);
    }
    switch (intWidth) {
        case 1:
            ZL_RET_T_IF_GT(
                    uint64_t,
                    node_invalid_input,
                    divisor,
                    UCHAR_MAX,
                    "Divisor too large");
            ZL_RET_T_IF_NE(
                    uint64_t,
                    node_invalid_input,
                    ZL_firstIndexNotDivisibleBy8(src, nbInts, divisor),
                    nbInts);
            break;
        case 2:
            ZL_RET_T_IF_GT(
                    uint64_t,
                    node_invalid_input,
                    divisor,
                    USHRT_MAX,
                    "Divisor too large");
            ZL_RET_T_IF_NE(
                    uint64_t,
                    node_invalid_input,
                    ZL_firstIndexNotDivisibleBy16(src, nbInts, divisor),
                    nbInts);
            break;
        case 4:
            ZL_RET_T_IF_GT(
                    uint64_t,
                    node_invalid_input,
                    divisor,
                    UINT_MAX,
                    "Divisor too large");
            ZL_RET_T_IF_NE(
                    uint64_t,
                    node_invalid_input,
                    ZL_firstIndexNotDivisibleBy32(src, nbInts, divisor),
                    nbInts);
            break;
        case 8:
            ZL_RET_T_IF_NE(
                    uint64_t,
                    node_invalid_input,
                    ZL_firstIndexNotDivisibleBy64(src, nbInts, divisor),
                    nbInts);
            break;
        default:
            ZL_ASSERT_FAIL("Unsupported int width");
    }
    return ZL_RESULT_WRAP_VALUE(uint64_t, divisor);
}

ZL_Report
EI_divide_by_int(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);
    size_t const intWidth = ZL_Input_eltWidth(in);
    ZL_ASSERT(intWidth == 1 || intWidth == 2 || intWidth == 4 || intWidth == 8);
    size_t const nbInts = ZL_Input_numElts(in);
    ZL_Output* const out =
            ZL_Encoder_createTypedStream(eictx, 0, nbInts, intWidth);
    ZL_ERR_IF_NULL(out, allocation);
    const void* src = ZL_Input_ptr(in);
    void* dst       = ZL_Output_ptr(out);

    uint8_t header[ZL_VARINT_LENGTH_64];
    ZL_RefParam div = ZL_Encoder_getLocalParam(eictx, ZL_DIVIDE_BY_PID);
    ZL_RESULT_OF(uint64_t)
    divisor = getDivisor(
            intWidth,
            nbInts,
            div.paramRef ? *(const uint64_t*)div.paramRef : 0,
            src);
    ZL_ERR_IF_ERR(divisor);
    uint64_t divisorValue = ZL_RES_value(divisor);
    ZS_divideByEncode(dst, src, nbInts, divisorValue, intWidth);
    size_t encodeSize = ZL_varintEncode64Fast(divisorValue, header);
    ZL_Encoder_sendCodecHeader(eictx, header, encodeSize);
    ZL_ERR_IF_ERR(ZL_Output_commit(out, nbInts));

    return ZL_returnSuccess();
}

ZL_NodeID ZL_Compressor_registerDivideByNode(
        ZL_Compressor* cgraph,
        uint64_t divisor)
{
    const ZL_CopyParam copyParam  = { .paramPtr  = &divisor,
                                      .paramSize = sizeof(divisor),
                                      .paramId   = ZL_DIVIDE_BY_PID };
    ZL_LocalCopyParams copyParams = { .copyParams   = &copyParam,
                                      .nbCopyParams = 1 };
    ZL_LocalParams localParams    = { .copyParams = copyParams };
    ZL_NodeID const node_divideBy =
            ZL_Compressor_cloneNode(cgraph, ZL_NODE_DIVIDE_BY, &localParams);
    return node_divideBy;
}

// Legacy interface

ZL_Report EI_divide_by_int_as_typedTransform(
        ZL_Encoder* eictx,
        const ZL_Input* in)
{
    return EI_divide_by_int(eictx, &in, 1);
}
