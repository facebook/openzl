// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/concat/decode_concat_binding.h"

#include "openzl/common/assertion.h"
#include "openzl/decompress/dictx.h" // DI_outStream_asReference
#include "openzl/shared/mem.h"       // ZL_memcpy
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"

ZL_Report DI_concat(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_EQ(nbVariableSrcs, 0);
    (void)variableSrcs;
    ZL_ASSERT_EQ(nbCompulsorySrcs, 2);
    ZL_ASSERT_NN(compulsorySrcs);
    const ZL_Input* const sizes = compulsorySrcs[0];
    ZL_ASSERT_NN(sizes);
    ZL_ERR_IF_NE(ZL_Input_type(sizes), ZL_Type_numeric, corruption);
    ZL_ERR_IF_NE(ZL_Input_eltWidth(sizes), sizeof(uint32_t), corruption);
    const ZL_Input* const concatenated = compulsorySrcs[1];
    ZL_ASSERT_NN(concatenated);

    ZL_Type const type    = ZL_Input_type(concatenated);
    size_t const eltWidth = ZL_Input_eltWidth(concatenated);
    size_t const nbElts   = ZL_Input_numElts(concatenated);
    const size_t nbRegens = ZL_Input_numElts(sizes);
    ZL_ERR_IF_EQ(nbRegens, 0, corruption);
    const uint32_t* const regenSizes = ZL_Input_ptr(sizes);

    size_t rPos = 0;
    ZL_ERR_IF_LT(nbRegens, dictx->nbRegens, corruption);

    if (type == ZL_Type_string) {
        const uint32_t* strLens = ZL_Input_stringLens(concatenated);
        size_t bytePos          = 0;
        for (size_t n = 0; n < nbRegens; n++) {
            size_t const rSize = regenSizes[n];
            ZL_ERR_IF_GT(rPos + rSize, nbElts, corruption);
            size_t byteSize = 0;
            for (size_t i = rPos; i < rPos + rSize; i++) {
                byteSize += strLens[i];
            }
            ZL_Output* out = DI_outStream_asReference(
                    dictx, (int)n, concatenated, bytePos, 1, byteSize);
            ZL_ERR_IF_NULL(out, allocation);
            uint32_t* regenStrLens = ZL_Output_reserveStringLens(out, rSize);
            /* TODO(T220688634): This can be avoided if we have API to reference
             * string lengths*/
            ZL_memcpy(regenStrLens, strLens + rPos, rSize * sizeof(uint32_t));
            ZL_ERR_IF_ERR(ZL_Output_commit(out, rSize));
            rPos += rSize;
            bytePos += byteSize;
        }
        ZL_ERR_IF_NE(rPos, nbElts, corruption);
    } else {
        for (size_t n = 0; n < nbRegens; n++) {
            size_t const rSize = regenSizes[n];
            ZL_ERR_IF_GT(
                    rPos + rSize * eltWidth, nbElts * eltWidth, corruption);
            ZL_Output* out = DI_outStream_asReference(
                    dictx, (int)n, concatenated, rPos, eltWidth, rSize);
            ZL_ERR_IF_NULL(out, allocation);
            rPos += rSize * eltWidth;
        }
        ZL_ERR_IF_NE(rPos, nbElts * eltWidth, corruption);
    }

    return ZL_returnSuccess();
}
