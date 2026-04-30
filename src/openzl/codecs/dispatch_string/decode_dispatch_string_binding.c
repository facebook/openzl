// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/dispatch_string/decode_dispatch_string_binding.h"

#include "openzl/codecs/dispatch_string/common_dispatch_string.h"
#include "openzl/codecs/dispatch_string/decode_dispatch_string_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/decompress/dictx.h"
#include "openzl/shared/histogram.h"

ZL_Report DI_dispatch_string(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_EQ(nbCompulsorySrcs, 1);
    ZL_ASSERT_NN(compulsorySrcs);
    ZL_ASSERT_NN(compulsorySrcs[0]);
    ZL_ASSERT_EQ(ZL_Input_type(compulsorySrcs[0]), ZL_Type_numeric);

    // 16-bit dispatch indices added in frame version 21
    bool is16bitDispatch = true;
    if (DI_getFrameFormatVersion(dictx) < 21) {
        is16bitDispatch = false;
    }

    if (is16bitDispatch) {
        ZL_ERR_IF_NE(
                ZL_Input_eltWidth(compulsorySrcs[0]),
                2,
                node_invalid_input); // dispatch indices are 16-bit
        ZL_ERR_IF_GT(
                nbVariableSrcs,
                ZL_DISPATCH_STRING_MAX_DISPATCHES,
                node_invalid,
                "Invalid number of streams");
    } else {
        ZL_ERR_IF_NE(
                ZL_Input_eltWidth(compulsorySrcs[0]), 1, node_invalid_input);
        ZL_ERR_IF_GT(
                nbVariableSrcs,
                ZL_DISPATCH_STRING_MAX_DISPATCHES_V20,
                node_invalid,
                "Invalid number of streams");
    }
    ZL_ASSERT_NN(variableSrcs);
    for (size_t i = 0; i < nbVariableSrcs; i++) {
        ZL_ASSERT_NN(variableSrcs[i]);
        ZL_ASSERT_EQ(ZL_Input_type(variableSrcs[i]), ZL_Type_string);
    }

    // validate index, src streams + build total buffer size
    const size_t nbStrs = ZL_Input_numElts(compulsorySrcs[0]);

    ZL_ERR_IF(
            (nbStrs != 0) && (nbVariableSrcs == 0),
            node_invalid_input,
            "Number of indices incompatible with number of streams");

    size_t totalSize = 0;

    if (ZL_LIKELY(nbVariableSrcs > 0)) {
        ZL_Histogram* histogram;
        if (is16bitDispatch) {
            const uint16_t* inputIndices = ZL_Input_ptr(compulsorySrcs[0]);
            histogram =
                    ZL_Decoder_getScratchSpace(dictx, sizeof(ZL_Histogram16));
            ZL_Histogram_init(histogram, ZL_DISPATCH_STRING_MAX_DISPATCHES);
            ZL_Histogram_build(histogram, inputIndices, nbStrs, 2);
            ZL_ERR_IF_GE(
                    histogram->maxSymbol,
                    nbVariableSrcs,
                    node_invalid_input,
                    "Invalid index stream");
        } else {
            const uint8_t* inputIndices = ZL_Input_ptr(compulsorySrcs[0]);

            histogram =
                    ZL_Decoder_getScratchSpace(dictx, sizeof(ZL_Histogram8));
            ZL_Histogram_init(histogram, 255);
            ZL_Histogram_build(histogram, inputIndices, nbStrs, 1);
            ZL_ERR_IF_GE(
                    histogram->maxSymbol,
                    nbVariableSrcs,
                    node_invalid_input,
                    "Invalid index stream");
        }
        for (size_t i = 0; i < nbVariableSrcs; i++) {
            totalSize += ZL_Input_contentSize(variableSrcs[i]);
            ZL_ERR_IF_NE(
                    ZL_Input_numElts(variableSrcs[i]),
                    histogram->count[i],
                    node_invalid_input,
                    "Index stream requires different input length than provided src[%u]",
                    i);
        }
    }

    // input stream massaging
    const char** srcBuffers =
            ZL_Decoder_getScratchSpace(dictx, nbVariableSrcs * sizeof(void*));
    ZL_ERR_IF_NULL(srcBuffers, allocation);
    const uint32_t** srcStrLens = ZL_Decoder_getScratchSpace(
            dictx, nbVariableSrcs * sizeof(uint32_t*));
    ZL_ERR_IF_NULL(srcStrLens, allocation);
    size_t* srcNbStrs =
            ZL_Decoder_getScratchSpace(dictx, nbVariableSrcs * sizeof(size_t));
    ZL_ERR_IF_NULL(srcNbStrs, allocation);
    for (size_t i = 0; i < nbVariableSrcs; i++) {
        srcBuffers[i] = ZL_Input_ptr(variableSrcs[i]);
        srcStrLens[i] = ZL_Input_stringLens(variableSrcs[i]);
        srcNbStrs[i]  = ZL_Input_numElts(variableSrcs[i]);
    }

    ZL_Output* const dst = ZL_Decoder_create1StringStream(
            dictx, nbStrs, totalSize + ZL_DISPATCH_STRING_BLK_SIZE);
    ZL_ERR_IF_NULL(dst, allocation);

    if (is16bitDispatch) {
        ZL_DispatchString_decode16(
                ZL_Output_ptr(dst),
                ZL_Output_stringLens(dst),
                nbStrs,
                (uint16_t)nbVariableSrcs,
                srcBuffers,
                srcStrLens,
                srcNbStrs,
                ZL_Input_ptr(compulsorySrcs[0]));
    } else {
        ZL_DispatchString_decode(
                ZL_Output_ptr(dst),
                ZL_Output_stringLens(dst),
                nbStrs,
                (uint8_t)nbVariableSrcs,
                srcBuffers,
                srcStrLens,
                srcNbStrs,
                ZL_Input_ptr(compulsorySrcs[0]));
    }
    ZL_ERR_IF_ERR(ZL_Output_commit(dst, nbStrs));

    return ZL_returnSuccess();
}
