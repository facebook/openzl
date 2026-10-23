// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "decode_divide_by_binding.h"
#include "decode_divide_by_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/numeric_operations.h" // NUMOP_findMax*
#include "openzl/shared/varint.h"             // ZL_varintDecode64Strict

#include <limits.h> // UCHAR_MAX, USHRT_MAX, UINT_MAX, ULLONG_MAX

ZL_Report DI_divide_by_int(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);
    size_t const intWidth = ZL_Input_eltWidth(in);
    ZL_ASSERT(intWidth == 1 || intWidth == 2 || intWidth == 4 || intWidth == 8);
    size_t const nbElts     = ZL_Input_numElts(in);
    ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dictx);

    void const* quotients = ZL_Input_ptr(in);
    if (header.size == 0) {
        ZL_ERR(corruption);
    }

    ZL_Output* const out = ZL_Decoder_create1OutStream(dictx, nbElts, intWidth);
    ZL_ERR_IF_NULL(out, allocation);
    const uint8_t* headerPtr = (const uint8_t*)header.start;
    ZL_RESULT_OF(uint64_t)
    const varint = ZL_varintDecode(
            &headerPtr, (const uint8_t*)header.start + header.size);
    ZL_TRY_LET(uint64_t, divisor, varint);
    ZL_ERR_IF_EQ(divisor, 0, node_invalid_input, "Attempt to divide by 0");
    // Quotients must not overflow once multiplied by the divisor.
    // Checking their maximum, rather than each quotient in a loop that exits
    // on error, lets the compiler vectorize the check.
    switch (intWidth) {
        case 1:
            ZL_ERR_IF_GT(divisor, UCHAR_MAX, node_invalid_input);
            ZL_ERR_IF_GT(
                    NUMOP_findMaxU8((const uint8_t*)quotients, nbElts),
                    UCHAR_MAX / divisor,
                    node_invalid_input);
            break;
        case 2:
            ZL_ERR_IF_GT(divisor, USHRT_MAX, node_invalid_input);
            ZL_ERR_IF_GT(
                    NUMOP_findMaxU16((const uint16_t*)quotients, nbElts),
                    USHRT_MAX / divisor,
                    node_invalid_input);
            break;
        case 4:
            ZL_ERR_IF_GT(divisor, UINT_MAX, node_invalid_input);
            ZL_ERR_IF_GT(
                    NUMOP_findMaxArr32((const uint32_t*)quotients, nbElts),
                    UINT_MAX / divisor,
                    node_invalid_input);
            break;
        case 8:
            ZL_ERR_IF_GT(
                    NUMOP_findMaxU64((const uint64_t*)quotients, nbElts),
                    ULLONG_MAX / divisor,
                    node_invalid_input);
            break;
        default:
            ZL_ASSERT_FAIL("Unsupported int width");
    }
    ZS_divideByDecode(ZL_Output_ptr(out), quotients, nbElts, divisor, intWidth);
    ZL_ERR_IF_ERR(ZL_Output_commit(out, nbElts));
    return ZL_returnSuccess();
}
