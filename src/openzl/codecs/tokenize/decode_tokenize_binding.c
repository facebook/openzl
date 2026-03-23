// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/tokenize/decode_tokenize_binding.h"

#include "openzl/codecs/tokenize/decode_tokenize_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/decompress/dictx.h"
#include "openzl/zl_data.h"
#include "openzl/zl_dtransform.h"

ZL_Report DI_tokenize(ZL_Decoder* dictx, const ZL_Input* in[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_Input const* const alphabet = in[0];
    ZL_Input const* const indices  = in[1];

    size_t const alphabetSize = ZL_Input_numElts(alphabet);
    size_t const eltWidth     = ZL_Input_eltWidth(alphabet);

    size_t const nbElts   = ZL_Input_numElts(indices);
    size_t const idxWidth = ZL_Input_eltWidth(indices);

    ZL_Output* out = ZL_Decoder_create1OutStream(dictx, nbElts, eltWidth);
    ZL_ERR_IF_NULL(out, allocation);

    bool const success = ZS_tokenizeDecode(
            ZL_Output_ptr(out),
            ZL_Input_ptr(alphabet),
            alphabetSize,
            ZL_Input_ptr(indices),
            nbElts,
            eltWidth,
            idxWidth);
    ZL_ERR_IF_NOT(success, corruption, "Tokenize detected corrupted input!");

    ZL_ERR_IF_ERR(ZL_Output_commit(out, nbElts));

    return ZL_returnValue(1);
}

ZL_Report DI_tokenizeVSF(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_NN(ins);

    const ZL_Input* const alphabet = ins[0];
    const ZL_Input* const indices  = ins[1];
    ZL_ASSERT_NN(alphabet);
    ZL_ASSERT_NN(indices);
    ZL_ASSERT(
            ZL_Input_type(alphabet) == ZL_Type_string
            && ZL_Input_type(indices) == ZL_Type_numeric);

    const uint8_t* const indicesSrc          = ZL_Input_ptr(indices);
    const uint32_t* const alphabetFieldSizes = ZL_Input_stringLens(alphabet);

    size_t const alphabetSize          = ZL_Input_numElts(alphabet);
    size_t const alphabetFieldSizesSum = ZL_Input_contentSize(alphabet);
    size_t const dstNbElts             = ZL_Input_numElts(indices);
    size_t const idxWidth              = ZL_Input_eltWidth(indices);

    ZL_ERR_IF_NOT(
            ZS_tokenizeValidateIndices(
                    alphabetSize, indicesSrc, dstNbElts, idxWidth),
            corruption);

    size_t const dstNbBytes = ZS_tokenizeComputeVSFContentSize(
            indicesSrc, idxWidth, dstNbElts, alphabetFieldSizes, alphabetSize);
    ZL_ERR_IF_LT(dstNbElts, alphabetSize, corruption);

    ZL_Output* const out = ZL_Decoder_create1OutStream(dictx, dstNbBytes, 1);
    ZL_ERR_IF_NULL(out, allocation);
    uint32_t* const dstFieldSizes = ZL_Output_reserveStringLens(out, dstNbElts);
    ZL_ERR_IF_NULL(dstFieldSizes, allocation);

    void* workspace = ZL_Decoder_getScratchSpace(
            dictx,
            ZS_tokenizeVSFDecodeWorkspaceSize(
                    alphabetSize, alphabetFieldSizesSum));
    ZL_ERR_IF_NULL(workspace, allocation);

    ZS_tokenizeVSFDecode(
            ZL_Input_ptr(alphabet),
            alphabetSize,
            indicesSrc,
            alphabetFieldSizes,
            alphabetFieldSizesSum,
            ZL_Output_ptr(out),
            dstFieldSizes,
            dstNbElts,
            dstNbBytes,
            idxWidth,
            workspace);

    ZL_ERR_IF_ERR(ZL_Output_commit(out, dstNbElts));
    return ZL_returnSuccess();
}
