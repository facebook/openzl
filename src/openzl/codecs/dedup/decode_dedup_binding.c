// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/dedup/decode_dedup_binding.h"

#include "openzl/common/assertion.h"
#include "openzl/decompress/dictx.h" // DI_outStream_asReference
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"

ZL_Report DI_dedup_num(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_EQ(nbVariableSrcs, 0);
    (void)variableSrcs;
    ZL_ASSERT_EQ(nbCompulsorySrcs, 1);
    ZL_ASSERT_NN(compulsorySrcs);
    const ZL_Input* const numSrc = compulsorySrcs[0];
    ZL_ASSERT_NN(numSrc);
    ZL_ASSERT_EQ(ZL_Input_type(numSrc), ZL_Type_numeric);
    size_t const eltWidth = ZL_Input_eltWidth(numSrc);
    size_t const eltCount = ZL_Input_numElts(numSrc);

    size_t const nbRegens = DI_getNbRegens(dictx);
    ZL_DLOG(BLOCK, "DI_dedup_num: nbRegens = %zu", nbRegens);

    for (size_t n = 0; n < nbRegens; n++) {
        ZL_Output* const out = DI_outStream_asReference(
                dictx, (int)n, numSrc, 0, eltWidth, eltCount);
        ZL_ERR_IF_NULL(out, allocation);
    }
    return ZL_returnSuccess();
}
