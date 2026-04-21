// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/sentinel/decode_sentinel_binding.h"

#include "openzl/codecs/sentinel/decode_sentinel_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_dtransform.h"

ZL_Report DI_sentinel(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_NN(ins);

    const ZL_Input* const valuesIn     = ins[0];
    const ZL_Input* const exceptionsIn = ins[1];

    ZL_ASSERT_EQ(ZL_Input_type(valuesIn), ZL_Type_numeric);
    ZL_ASSERT_EQ(ZL_Input_type(exceptionsIn), ZL_Type_numeric);

    const size_t valWidth      = ZL_Input_eltWidth(valuesIn);
    const size_t outWidth      = ZL_Input_eltWidth(exceptionsIn);
    const size_t numElts       = ZL_Input_numElts(valuesIn);
    const size_t numExceptions = ZL_Input_numElts(exceptionsIn);

    ZL_ERR_IF_GT(valWidth, outWidth, corruption, "values width > output width");

    /* Read codec header to determine sentinel */
    const ZL_RBuffer header = ZL_Decoder_getCodecHeader(dictx);

    const uint64_t maxVal = ZL_maxValueForWidth(valWidth);
    uint64_t sentinel     = maxVal;

    if (header.size > 0) {
        const uint8_t* hdr = (const uint8_t*)header.start;
        ZL_TRY_LET(uint64_t, decoded, ZL_varintDecode(&hdr, hdr + header.size));
        sentinel = decoded;
        ZL_ERR_IF_GT(
                sentinel, maxVal, corruption, "sentinel exceeds values width");
    }

    /* Create output stream at outWidth */
    ZL_Output* const out =
            ZL_Decoder_create1OutStream(dictx, numElts, outWidth);
    ZL_ERR_IF_NULL(out, allocation);

    const int rc = ZL_sentinelDecode(
            ZL_Output_ptr(out),
            ZL_Input_ptr(valuesIn),
            ZL_Input_ptr(exceptionsIn),
            numElts,
            numExceptions,
            valWidth,
            outWidth,
            sentinel);
    ZL_ERR_IF_NE(rc, 0, corruption, "sentinel decode failed");

    ZL_ERR_IF_ERR(ZL_Output_commit(out, numElts));

    return ZL_returnSuccess();
}
