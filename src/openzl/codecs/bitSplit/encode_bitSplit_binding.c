// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bitSplit/encode_bitSplit_binding.h"
#include "openzl/codecs/bitSplit/common_bitSplit_kernel.h" /* ZL_bitSplit_outputEltWidth */
#include "openzl/codecs/bitSplit/encode_bitSplit_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/logging.h"
#include "openzl/compress/enc_interface.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/shared/mem.h" // ZL_memcpy
#include "openzl/zl_data.h"
#include "openzl/zl_graph_api.h"

// Parameter IDs for bitSplit
#define ZL_BITSPLIT_WIDTHS_PID 701
#define ZL_BITSPLIT_NBWIDTHS_PID 702

ZL_Report EI_bitSplit_withWidths(
        ZL_Encoder* eictx,
        const ZL_Input* in,
        const uint8_t* bitWidths,
        size_t nbWidths)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);
    ZL_ASSERT_NN(bitWidths);
    ZL_ASSERT_GT(nbWidths, 0);
    ZL_ASSERT_LE(nbWidths, 64);

    size_t const inputEltWidth = ZL_Input_eltWidth(in);
    ZL_ASSERT(
            inputEltWidth == 1 || inputEltWidth == 2 || inputEltWidth == 4
            || inputEltWidth == 8);
    size_t const inputEltWidthBits = inputEltWidth * 8;
    size_t const nbElts            = ZL_Input_numElts(in);

    // Validate parameters
    size_t sumWidths = 0;
    ZL_ERR_IF(
            !ZL_bitSplit_paramsAreValid(
                    bitWidths, nbWidths, inputEltWidthBits, &sumWidths),
            nodeParameter_invalid,
            "bitSplit parameter validation failed");

    // Validate top bits are zero if partial coverage
    if (sumWidths < inputEltWidthBits) {
        ZL_ERR_IF(
                !ZL_bitSplit_topBitsAreZero(
                        ZL_Input_ptr(in), inputEltWidth, nbElts, sumWidths),
                corruption,
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
        ZL_memcpy(header + 1, bitWidths, nbWidths - 1);
        headerSize = 1 + nbWidths - 1;
    } else {
        // Partial coverage: send all widths (decoder adds implicit padding)
        ZL_memcpy(header + 1, bitWidths, nbWidths);
        headerSize = 1 + nbWidths;
    }

    ZL_Encoder_sendCodecHeader(eictx, header, headerSize);

    // Create output streams
    ZL_Output* outputs[64];
    size_t outputWidths[64];
    void* dstPtrs[64];

    for (size_t i = 0; i < nbWidths; i++) {
        outputWidths[i] = ZL_bitSplit_outputEltWidth(bitWidths[i]);
        outputs[i] =
                ZL_Encoder_createTypedStream(eictx, 0, nbElts, outputWidths[i]);
        ZL_ERR_IF_NULL(outputs[i], allocation);
        dstPtrs[i] = ZL_Output_ptr(outputs[i]);
    }

    // Kernel owns the hot loop - single call processes all elements
    ZL_bitSplitEncode(
            dstPtrs,
            outputWidths,
            nbElts,
            ZL_Input_ptr(in),
            inputEltWidth,
            bitWidths,
            nbWidths);

    // Commit all outputs
    for (size_t i = 0; i < nbWidths; i++) {
        ZL_ERR_IF_ERR(ZL_Output_commit(outputs[i], nbElts));
    }

    return ZL_returnValue(nbWidths);
}

ZL_Report EI_bitSplit(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
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

    ZL_ERR_IF_EQ(
            widthsParam.paramId,
            ZL_LP_INVALID_PARAMID,
            nodeParameter_invalid,
            "bitSplit requires bit widths parameter");
    ZL_ERR_IF_EQ(
            nbWidthsParam.paramId,
            ZL_LP_INVALID_PARAMID,
            nodeParameter_invalid,
            "bitSplit requires nbWidths parameter");
    ZL_ERR_IF_NULL(
            widthsParam.paramRef,
            nodeParameter_invalid,
            "bitSplit bit widths parameter is NULL");

    const uint8_t* bitWidths = (const uint8_t*)widthsParam.paramRef;
    size_t const nbWidths    = (size_t)nbWidthsParam.paramValue;

    // Validate: must have at least one width
    ZL_ERR_IF_EQ(
            nbWidths,
            0,
            nodeParameter_invalid,
            "bitSplit requires at least one bit width parameter");

    return EI_bitSplit_withWidths(eictx, in, bitWidths, nbWidths);
}

ZL_NodeID ZL_Compressor_registerBitSplitNode(
        ZL_Compressor* compressor,
        const uint8_t* bitWidths,
        size_t nbWidths)
{
    ZL_RESULT_OF(ZL_NodeID)
    const result =
            ZL_Compressor_buildBitSplitNode(compressor, bitWidths, nbWidths);
    if (ZL_RES_isError(result))
        return ZL_NODE_ILLEGAL;
    return ZL_RES_value(result);
}

ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_buildBitSplitNode(
        ZL_Compressor* compressor,
        const uint8_t* bitWidths,
        size_t nbWidths)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_NodeID, compressor);

    ZL_ERR_IF_EQ(
            nbWidths,
            0,
            nodeParameter_invalid,
            "bitSplit requires at least one bit width");
    ZL_ERR_IF_GT(
            nbWidths,
            64,
            nodeParameter_invalid,
            "bitSplit supports at most 64 bit widths");

    ZL_CopyParam const widthsParam = { .paramId   = ZL_BITSPLIT_WIDTHS_PID,
                                       .paramPtr  = bitWidths,
                                       .paramSize = nbWidths };
    ZL_LocalCopyParams const lgp   = { &widthsParam, 1 };

    ZL_IntParam const nbWidthsParam = {
        .paramId    = ZL_BITSPLIT_NBWIDTHS_PID,
        .paramValue = (int)nbWidths,
    };
    ZL_LocalIntParams const lip     = { &nbWidthsParam, 1 };
    ZL_LocalParams const lParams    = { .copyParams = lgp, .intParams = lip };
    ZL_NodeParameters const nParams = { .localParams = &lParams };

    return ZL_Compressor_parameterizeNode(
            compressor, ZL_NODE_BITSPLIT, &nParams);
}
