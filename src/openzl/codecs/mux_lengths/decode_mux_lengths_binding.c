// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "decode_mux_lengths_binding.h"

#include "decode_mux_lengths_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/zl_dtransform.h"

ZL_Report DI_mux_lengths(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_NN(ins);

    // Decoder inputs are encoder outputs:
    // ins[0] = muxed_lengths (serial)
    // ins[1] = long_lengths (numeric)
    const ZL_Input* muxedIn = ins[0];
    const ZL_Input* longIn  = ins[1];

    ZL_ASSERT_NN(muxedIn);
    ZL_ASSERT_NN(longIn);

    size_t const numMuxed = ZL_Input_numElts(muxedIn);
    size_t const eltWidth = ZL_Input_eltWidth(longIn);
    size_t const numLong  = ZL_Input_numElts(longIn);

    // Parse codec header
    ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dictx);
    ZL_ERR_IF_NE(
            header.size, 1, corruption, "mux_lengths header must be 1 byte");
    uint8_t const headerByte       = *(const uint8_t*)header.start;
    unsigned const splitPoint      = headerByte & 0x0F;
    unsigned const matchLengthBias = headerByte >> 4;

    ZL_ERR_IF_GT(splitPoint, 8, corruption, "split_point must be in [0, 8]");

    // Create output streams (decoder outputs are encoder inputs):
    // Output 0: literal_lengths (numeric)
    // Output 1: match_lengths (numeric)
    ZL_Output* const llOut =
            ZL_Decoder_createTypedStream(dictx, 0, numMuxed, eltWidth);
    ZL_ERR_IF_NULL(llOut, allocation);

    ZL_Output* const mlOut =
            ZL_Decoder_createTypedStream(dictx, 1, numMuxed, eltWidth);
    ZL_ERR_IF_NULL(mlOut, allocation);

    // Decode
    ZL_MuxLengthsError const muxErr = ZL_muxLengthsDecode(
            ZL_Output_ptr(llOut),
            ZL_Output_ptr(mlOut),
            (const uint8_t*)ZL_Input_ptr(muxedIn),
            numMuxed,
            ZL_Input_ptr(longIn),
            numLong,
            eltWidth,
            splitPoint,
            matchLengthBias);
    ZL_ERR_IF_NE(
            muxErr,
            ZL_MuxLengthsError_ok,
            corruption,
            "mux_lengths decode failed");

    ZL_ERR_IF_ERR(ZL_Output_commit(llOut, numMuxed));
    ZL_ERR_IF_ERR(ZL_Output_commit(mlOut, numMuxed));

    return ZL_returnSuccess();
}
