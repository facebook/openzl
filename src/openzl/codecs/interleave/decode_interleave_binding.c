// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/interleave/decode_interleave_binding.h"

#include "openzl/codecs/interleave/common_interleave.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_input.h"

ZL_Report DI_interleave(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);

    ZL_ERR_IF_NE(
            nbCompulsorySrcs,
            1,
            corruption,
            "interleave decoder expects 1 input");
    ZL_ERR_IF_NE(
            nbVariableSrcs,
            0,
            corruption,
            "interleave decoder expects no variable inputs");
    (void)variableSrcs;
    ZL_ERR_IF_NULL(compulsorySrcs[0], corruption);
    const ZL_Input* in = compulsorySrcs[0];
    ZL_ERR_IF_NE(
            ZL_Input_type(in),
            ZL_Type_string,
            temporaryLibraryLimitation,
            "Only string input is supported");

    const ZL_RBuffer e = ZL_Decoder_getCodecHeader(dictx);
    uint32_t nbStreams;
    ZL_ERR_IF_NE(e.size, sizeof(nbStreams), corruption, "invalid header size");
    memcpy(&nbStreams, e.start, e.size);
    ZL_ERR_IF_EQ(nbStreams, 0, corruption, "nbStreams must be > 0");
    ZL_ERR_IF_GT(
            nbStreams,
            ZL_INTERLEAVE_MAX_INPUTS,
            corruption,
            "nbStreams too large");
    ZL_ERR_IF_NE(
            ZL_Input_numElts(in) % nbStreams,
            0,
            corruption,
            "input size must be a multiple of nbStreams");
    const size_t nbStringsPerStream = ZL_Input_numElts(in) / nbStreams;

    ZL_Output** regen =
            ZL_Decoder_getScratchSpace(dictx, nbStreams * sizeof(ZL_Output*));
    ZL_ERR_IF_NULL(regen, allocation);
    for (size_t i = 0; i < nbStreams; ++i) {
        regen[i] = ZL_Decoder_createStringStream(
                dictx, (int)i, nbStringsPerStream, ZL_Input_contentSize(in));
        ZL_ERR_IF_NULL(regen[i], allocation);
    }
    char** dstPtrs =
            ZL_Decoder_getScratchSpace(dictx, nbStreams * sizeof(char*));
    ZL_ERR_IF_NULL(dstPtrs, allocation);
    for (size_t i = 0; i < nbStreams; ++i) {
        dstPtrs[i] = ZL_Output_ptr(regen[i]);
        ZL_ERR_IF_NULL(dstPtrs[i], allocation);
    }
    uint32_t** dstStringLens =
            ZL_Decoder_getScratchSpace(dictx, nbStreams * sizeof(uint32_t*));
    ZL_ERR_IF_NULL(dstStringLens, allocation);
    for (size_t i = 0; i < nbStreams; ++i) {
        dstStringLens[i] = ZL_Output_stringLens(regen[i]);
        ZL_ERR_IF_NULL(dstStringLens[i], allocation);
    }

    const uint8_t* src            = ZL_Input_ptr(in);
    const uint32_t* srcStringLens = ZL_Input_stringLens(in);

    for (size_t i = 0; i < nbStringsPerStream; ++i) {
        for (size_t j = 0; j < nbStreams; ++j) {
            memcpy(dstPtrs[j], src, *srcStringLens);
            *dstStringLens[j] = *srcStringLens;
            dstPtrs[j] += *srcStringLens;
            src += *srcStringLens;
            ++srcStringLens;
            ++dstStringLens[j];
        }
    }
    for (size_t i = 0; i < nbStreams; ++i) {
        ZL_ERR_IF_ERR(ZL_Output_commit(regen[i], nbStringsPerStream));
    }

    return ZL_WRAP_VALUE(0);
}
