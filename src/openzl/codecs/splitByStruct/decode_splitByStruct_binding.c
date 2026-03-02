// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/splitByStruct/decode_splitByStruct_binding.h" // DI_splitByStruct
#include "openzl/codecs/splitByStruct/decode_splitByStruct_kernel.h" // ZS_dispatchArrayFixedSizeStruct_decode
#include "openzl/common/assertion.h"
#include "openzl/common/logging.h"   // ZL_DLOG
#include "openzl/decompress/dictx.h" // ZL_Decoder_getScratchSpace
#include "openzl/zl_errors.h"

ZL_Report DI_splitByStruct(
        ZL_Decoder* dictx,
        const ZL_Input* inFixed[],
        size_t nbInFixed,
        const ZL_Input* inFields[],
        size_t nbInFields)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_DLOG(BLOCK, "DI_splitByStruct: combine %zu fs-fields", nbInFields);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_EQ(nbInFixed, 0);
    (void)inFixed;
    ZL_ERR_IF_EQ(
            nbInFields,
            0,
            corruption,
            "Split by struct must have at least one field");
    ZL_ASSERT_NN(inFields);
    ZL_ASSERT_GT(nbInFields, 0);

    size_t structSize   = 0;
    size_t const nbElts = ZL_Input_numElts(inFields[0]);
    for (size_t n = 0; n < nbInFields; n++) {
        ZL_ASSERT_NN(inFields[n]);
        ZL_ERR_IF_NE(
                ZL_Input_type(inFields[n]),
                ZL_Type_struct,
                corruption,
                "DI_splitByStruct decoder transform can only ingest ZL_Type_struct streams");
        ZL_ERR_IF_NE(
                ZL_Input_numElts(inFields[n]),
                nbElts,
                corruption,
                "DI_splitByStruct decoder can only work if all input streams have same nb of elts");
        structSize += ZL_Input_eltWidth(inFields[n]);
    }
    size_t const dstSize = nbElts * structSize;
    ZL_Output* const out = ZL_Decoder_create1OutStream(dictx, dstSize, 1);
    ZL_ERR_IF_NULL(out, allocation);

    // Note : could employ a workspace instead, when available
    size_t* const fieldSizes =
            ZL_Decoder_getScratchSpace(dictx, nbInFields * sizeof(*fieldSizes));
    const void** const fieldIns =
            ZL_Decoder_getScratchSpace(dictx, nbInFields * sizeof(*fieldIns));
    if (fieldSizes == NULL || fieldIns == NULL) {
        ZL_ERR(allocation,
               "DI_splitByStruct: unable to allocate for structure definitions");
    }
    for (size_t n = 0; n < nbInFields; n++) {
        fieldSizes[n] = ZL_Input_eltWidth(inFields[n]);
        fieldIns[n]   = ZL_Input_ptr(inFields[n]);
    }

    size_t const r = ZS_dispatchArrayFixedSizeStruct_decode(
            ZL_Output_ptr(out),
            dstSize,
            fieldIns,
            fieldSizes,
            nbInFields,
            nbElts);
    ZL_ASSERT_EQ(r, dstSize);
    ZL_ERR_IF_ERR(ZL_Output_commit(out, dstSize));

    return ZL_returnSuccess();
}
