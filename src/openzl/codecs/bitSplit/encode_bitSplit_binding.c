// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <string.h>

#include "openzl/codecs/bitSplit/encode_bitSplit_binding.h"
#include "openzl/codecs/bitSplit/encode_bitSplit_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/logging.h"
#include "openzl/compress/enc_interface.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_data.h"
#include "openzl/zl_graph_api.h"

// Parameter IDs for bitSplit
#define ZL_BITSPLIT_WIDTHS_PID 701
#define ZL_BITSPLIT_NBWIDTHS_PID 702

ZL_Report EI_bitSplit(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);

    // Get parameters from local params
    ZL_RefParam const widthsParam =
            ZL_Encoder_getLocalParam(eictx, ZL_BITSPLIT_WIDTHS_PID);
    ZL_IntParam const nbWidthsParam =
            ZL_Encoder_getLocalIntParam(eictx, ZL_BITSPLIT_NBWIDTHS_PID);

    ZL_RET_R_IF_EQ(
            nodeParameter_invalid,
            widthsParam.paramId,
            ZL_LP_INVALID_PARAMID,
            "bitSplit requires bit widths parameter");
    ZL_RET_R_IF_EQ(
            nodeParameter_invalid,
            nbWidthsParam.paramId,
            ZL_LP_INVALID_PARAMID,
            "bitSplit requires nbWidths parameter");
    ZL_RET_R_IF_NULL(
            nodeParameter_invalid,
            widthsParam.paramRef,
            "bitSplit bit widths parameter is NULL");

    const uint8_t* bitWidths = (const uint8_t*)widthsParam.paramRef;
    size_t const nbWidths    = (size_t)nbWidthsParam.paramValue;

    // Validate: must have at least one width
    ZL_RET_R_IF_EQ(
            nodeParameter_invalid,
            nbWidths,
            0,
            "bitSplit requires at least one bit width parameter");

    size_t const inputEltWidth = ZL_Input_eltWidth(in);
    ZL_ASSERT(
            inputEltWidth == 1 || inputEltWidth == 2 || inputEltWidth == 4
            || inputEltWidth == 8);
    size_t const inputEltWidthBits = inputEltWidth * 8;
    size_t const nbElts            = ZL_Input_numElts(in);

    // Validate parameters
    size_t sumWidths           = 0;
    int const validationResult = ZS_bitSplit_validateParams(
            bitWidths, nbWidths, inputEltWidthBits, &sumWidths);
    ZL_RET_R_IF_NE(
            nodeParameter_invalid,
            validationResult,
            0,
            "bitSplit parameter validation failed");

    // Validate top bits are zero if partial coverage
    if (sumWidths < inputEltWidthBits) {
        ZL_RET_R_IF_EQ(
                corruption,
                ZS_bitSplit_topBitsAreZero(
                        ZL_Input_ptr(in), inputEltWidth, nbElts, sumWidths),
                0,
                "bitSplit: top bits must be zero for partial coverage");
    }

    // Build header: [inputEltWidth] [widths...]
    // Full coverage: omit last width (computed by decoder)
    // Partial coverage: send all widths (decoder adds implicit padding)
    uint8_t header[65];
    header[0] = (uint8_t)inputEltWidth;
    size_t headerSize;

    if (sumWidths == inputEltWidthBits) {
        // Full coverage: omit last width (computed by decoder)
        memcpy(header + 1, bitWidths, nbWidths - 1);
        headerSize = 1 + nbWidths - 1;
    } else {
        // Partial coverage: send all widths (decoder adds implicit padding)
        memcpy(header + 1, bitWidths, nbWidths);
        headerSize = 1 + nbWidths;
    }

    ZL_Encoder_sendCodecHeader(eictx, header, headerSize);

    // Create output streams
    ZL_Output* outputs[64];
    size_t outputWidths[64];
    void* dstPtrs[64];
    ZL_ASSERT_LE(nbWidths, 64);

    for (size_t i = 0; i < nbWidths; i++) {
        outputWidths[i] = ZS_bitSplit_outputEltWidth(bitWidths[i]);
        outputs[i] =
                ZL_Encoder_createTypedStream(eictx, 0, nbElts, outputWidths[i]);
        ZL_RET_R_IF_NULL(allocation, outputs[i]);
        dstPtrs[i] = ZL_Output_ptr(outputs[i]);
    }

    // Kernel owns the hot loop - single call processes all elements
    ZS_bitSplitEncode(
            ZL_Input_ptr(in),
            inputEltWidth,
            nbElts,
            bitWidths,
            nbWidths,
            dstPtrs,
            outputWidths);

    // Commit all outputs
    for (size_t i = 0; i < nbWidths; i++) {
        ZL_RET_R_IF_ERR(ZL_Output_commit(outputs[i], nbElts));
    }

    return ZL_returnValue(nbWidths);
}

ZL_NodeID ZL_Compressor_registerBitSplitNode(
        ZL_Compressor* cgraph,
        const uint8_t* bitWidths,
        size_t nbWidths)
{
    if (nbWidths == 0) {
        ZL_LOG(ERROR, "bitSplit requires at least one bit width");
        return ZL_NODE_ILLEGAL;
    }
    if (nbWidths > 64) {
        ZL_LOG(ERROR, "bitSplit supports at most 64 bit widths");
        return ZL_NODE_ILLEGAL;
    }

    ZL_CopyParam const widthsParam = { .paramId   = ZL_BITSPLIT_WIDTHS_PID,
                                       .paramPtr  = bitWidths,
                                       .paramSize = nbWidths };
    ZL_LocalCopyParams const lgp   = { &widthsParam, 1 };

    ZL_IntParam const nbWidthsParam = {
        .paramId    = ZL_BITSPLIT_NBWIDTHS_PID,
        .paramValue = (int)nbWidths,
    };
    ZL_LocalIntParams const lip = { &nbWidthsParam, 1 };

    ZL_LocalParams const lParams = { .copyParams = lgp, .intParams = lip };
    return ZL_Compressor_cloneNode(
            cgraph, (ZL_NodeID){ ZL_PrivateStandardNodeID_bitSplit }, &lParams);
}
