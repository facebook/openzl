// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/range_pack/decode_range_pack_binding.h"
#include "openzl/codecs/range_pack/decode_range_pack_kernel.h"

#include "openzl/common/assertion.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/utils.h"
#include "openzl/zl_data.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_errors.h"

ZL_Report DI_rangePack(ZL_Decoder* dictx, const ZL_Input* streams[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(streams);
    const ZL_Input* in = streams[0];
    ZL_ASSERT(ZL_Input_type(in) == ZL_Type_numeric);
    const void* src       = ZL_Input_ptr(in);
    const size_t srcWidth = ZL_Input_eltWidth(in);
    const size_t nbElts   = ZL_Input_numElts(in);

    const ZL_RBuffer header = ZL_Decoder_getCodecHeader(dictx);
    ZL_ERR_IF_LT(header.size, 1, corruption, "Range decoder expects a header");
    uint8_t dstWidth = *(const uint8_t*)header.start;
    ZL_ERR_IF_LT(
            dstWidth,
            srcWidth,
            corruption,
            "Range pack decoder expects dst to contain src");
    ZL_ERR_IF(
            !ZL_isLegalIntegerWidth(dstWidth),
            corruption,
            "Range pack decoder got an illegal dstWidth (%zu)",
            dstWidth);
    uint64_t minValue = 0;
    if (header.size > 1) {
        if (header.size != (size_t)(dstWidth + 1)) {
            ZL_ERR(corruption,
                   "Range pack decoder header should be either 1 or 1+dstWidth bytes");
        }
        minValue = ZL_readLE64_N((const uint8_t*)header.start + 1, dstWidth);
    }
    ZL_Output* dstStream = ZL_Decoder_create1OutStream(dictx, nbElts, dstWidth);
    ZL_ERR_IF(!dstStream, allocation);
    void* dst = ZL_Output_ptr(dstStream);
    rangePackDecode(dst, dstWidth, src, srcWidth, nbElts, minValue);

    ZL_ERR_IF_ERR(ZL_Output_commit(dstStream, nbElts));
    return ZL_returnValue(1);
}
