// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/delta/encode_delta_binding.h"
#include "openzl/codecs/delta/encode_delta_kernel.h" // ZS_deltaEncodeXX
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/zl_data.h"

// ZL_TypedEncoderFn
// This variant is compatible with any allowed integer width
ZL_Report EI_delta_int(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);
    size_t const intWidth = ZL_Input_eltWidth(in);
    ZL_ASSERT(intWidth == 1 || intWidth == 2 || intWidth == 4 || intWidth == 8);
    size_t const nbInts = ZL_Input_numElts(in);
    ZL_Output* const out =
            ZL_Encoder_createTypedStream(eictx, 0, nbInts, intWidth);
    ZL_ERR_IF_NULL(out, allocation);
    const void* src = ZL_Input_ptr(in);
    // Note : proper alignment is guaranteed by graph engine
    void* dst = ZL_Output_ptr(out);
    if (nbInts == 0) {
        // Special case: Zero ints behaves the same for both variants.
        ZL_ERR_IF_ERR(ZL_Output_commit(out, 0));
    } else if (ZL_Encoder_getCParam(eictx, ZL_CParam_formatVersion) < 13) {
        // Old variant: Write the first element to the first value in the stream
        ZS_deltaEncode(dst, (char*)dst + intWidth, src, nbInts, intWidth);
        ZL_ERR_IF_ERR(ZL_Output_commit(out, nbInts));
    } else {
        // New variant: Write the first element to the stream header
        uint8_t header[8];
        ZL_ASSERT_GT(nbInts, 0);
        ZL_ASSERT_LE(intWidth, sizeof(header));
        ZS_deltaEncode(header, dst, src, nbInts, intWidth);
        ZL_Encoder_sendCodecHeader(eictx, header, intWidth);
        ZL_ERR_IF_ERR(ZL_Output_commit(out, nbInts - 1));
    }
    return ZL_returnValue(1);
}
