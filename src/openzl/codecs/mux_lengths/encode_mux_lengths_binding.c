// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "encode_mux_lengths_binding.h"

#include "encode_mux_lengths_kernel.h"
#include "openzl/codecs/zl_lz.h"
#include "openzl/codecs/zl_mux_lengths.h"
#include "openzl/common/assertion.h"
#include "openzl/zl_ctransform.h"

ZL_Report EI_mux_lengths(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbIns, 2);

    const ZL_Input* llIn = ins[0];
    const ZL_Input* mlIn = ins[1];

    size_t const eltWidth    = ZL_Input_eltWidth(llIn);
    size_t const numElements = ZL_Input_numElts(llIn);
    ZL_ERR_IF_NE(
            eltWidth,
            ZL_Input_eltWidth(mlIn),
            node_invalid_input,
            "literal and match lengths must have same element width");
    ZL_ERR_IF_NE(
            numElements,
            ZL_Input_numElts(mlIn),
            node_invalid_input,
            "literal and match lengths must have same element count");

    // Read match_length_bias: from parameter, or from match length metadata,
    // or default to 0.
    unsigned matchLengthBias;
    ZL_IntParam mlbParam = ZL_Encoder_getLocalIntParam(
            eictx, ZL_MUX_LENGTHS_MATCH_LENGTH_BIAS_PID);
    if (mlbParam.paramId != ZL_LP_INVALID_PARAMID) {
        matchLengthBias = (unsigned)mlbParam.paramValue;
    } else {
        ZL_IntMetadata const meta = ZL_Input_getIntMetadata(
                mlIn, ZL_LZ_MIN_MATCH_LENGTH_METADATA_ID);
        matchLengthBias = meta.isPresent ? (unsigned)meta.mValue : 0;
    }
    ZL_ERR_IF_GT(
            matchLengthBias,
            15,
            nodeParameter_invalid,
            "match_length_bias must be in [0, 15]");

    // Read split_point: from parameter, or auto-compute.
    unsigned splitPoint;
    size_t numLong;
    ZL_IntParam spParam =
            ZL_Encoder_getLocalIntParam(eictx, ZL_MUX_LENGTHS_SPLIT_POINT_PID);
    if (spParam.paramId != ZL_LP_INVALID_PARAMID) {
        splitPoint = (unsigned)spParam.paramValue;
        ZL_ERR_IF_GT(
                splitPoint,
                8,
                nodeParameter_invalid,
                "split_point must be in [0, 8]");
        numLong = ZL_muxLengthsCountLong(
                ZL_Input_ptr(llIn),
                ZL_Input_ptr(mlIn),
                numElements,
                eltWidth,
                splitPoint,
                matchLengthBias);
    } else {
        ZL_MuxLengthsSplitResult const result = ZL_muxLengthsComputeSplitPoint(
                ZL_Input_ptr(llIn),
                ZL_Input_ptr(mlIn),
                numElements,
                eltWidth,
                matchLengthBias);
        splitPoint = (unsigned)result.splitPoint;
        numLong    = result.numLong;
    }

    // Create output streams
    // Output 0: muxed bytes (serial, 1 byte per element)
    ZL_Output* muxedOut =
            ZL_Encoder_createTypedStream(eictx, 0, numElements, 1);
    ZL_ERR_IF_NULL(muxedOut, allocation);

    // Output 1: long lengths (numeric, same width as input)
    // + padding for SIMD store overshoot in the encode kernel.
    ZL_Output* longOut = ZL_Encoder_createTypedStream(
            eictx, 1, numLong + ZL_MUX_LONG_SLOP_ELTS, eltWidth);
    ZL_ERR_IF_NULL(longOut, allocation);

    // Encode
    const size_t longCount = ZL_muxLengthsEncode(
            (uint8_t*)ZL_Output_ptr(muxedOut),
            ZL_Output_ptr(longOut),
            ZL_Input_ptr(llIn),
            ZL_Input_ptr(mlIn),
            numElements,
            eltWidth,
            splitPoint,
            matchLengthBias);

    // Send codec header: 1 byte
    uint8_t header = (uint8_t)(splitPoint | (matchLengthBias << 4));
    ZL_Encoder_sendCodecHeader(eictx, &header, 1);

    // Commit outputs
    ZL_ERR_IF_ERR(ZL_Output_commit(muxedOut, numElements));
    ZL_ERR_IF_ERR(ZL_Output_commit(longOut, longCount));

    return ZL_returnSuccess();
}
