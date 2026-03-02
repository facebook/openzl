// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/range_pack/encode_range_pack_binding.h"
#include "openzl/codecs/range_pack/encode_range_pack_kernel.h"

#include "openzl/common/errors_internal.h"
#include "openzl/shared/estimate.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/numeric_operations.h" // NUMOP_*
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"

ZL_Report EI_rangePack(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in    = ins[0];
    void const* src       = ZL_Input_ptr(in);
    size_t const srcWidth = ZL_Input_eltWidth(in);
    size_t const nbElts   = ZL_Input_numElts(in);
    ZL_ElementRange const range =
            ZL_computeUnsignedRange(src, nbElts, srcWidth);
    const size_t dstWidth = NUMOP_numericWidthForValue(range.max - range.min);

    ZL_Output* const dstStream =
            ZL_Encoder_createTypedStream(eictx, 0, nbElts, dstWidth);
    ZL_ERR_IF(!dstStream, allocation);
    void* dst = ZL_Output_ptr(dstStream);
    rangePackEncode(dst, dstWidth, src, srcWidth, nbElts, range.min);
    ZL_ERR_IF_ERR(ZL_Output_commit(dstStream, nbElts));

    /* Header has the source size and the min value if needed */
    uint8_t header[9];
    size_t header_size = 1;
    header[0]          = (uint8_t)srcWidth;
    if (range.min != 0) {
        ZL_writeLE64_N(header + 1, (uint64_t)range.min, srcWidth);
        header_size += srcWidth;
    }
    ZL_Encoder_sendCodecHeader(eictx, &header, header_size);
    return ZL_returnValue(1);
}
