// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <string.h>

#include "openzl/codecs/bitSplit/common_bitSplit_kernel.h"
#include "openzl/codecs/bitSplit/decode_bitSplit_binding.h"
#include "openzl/codecs/bitSplit/decode_bitSplit_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/decompress/dictx.h"
#include "src/openzl/shared/mem.h" // ZL_memcpy

ZL_Report DI_bitSplit(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs)
{
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_EQ(nbCompulsorySrcs, 0);
    (void)compulsorySrcs;
    (void)nbCompulsorySrcs;

    // Get parameters from codec header
    // Header format: [outputEltWidth (1 byte)] [stored widths...]
    ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dictx);

    // Validate: header must have at least outputEltWidth byte
    ZL_RET_R_IF_LT(
            corruption,
            header.size,
            1,
            "bitSplit: header must contain outputEltWidth");

    // Parse header
    uint8_t const outputEltWidth = ((const uint8_t*)header.start)[0];
    size_t const nbStoredWidths  = header.size - 1;
    const uint8_t* storedWidths  = (const uint8_t*)header.start + 1;

    // Validate outputEltWidth
    ZL_RET_R_IF_NOT(
            corruption,
            outputEltWidth == 1 || outputEltWidth == 2 || outputEltWidth == 4
                    || outputEltWidth == 8,
            "bitSplit: invalid outputEltWidth in header");

    // Compute sum of stored widths and determine last width
    size_t sumStoredWidths = 0;
    for (size_t i = 0; i < nbStoredWidths; i++) {
        ZL_RET_R_IF_EQ(
                corruption,
                storedWidths[i],
                0,
                "bitSplit: bit width cannot be zero");
        sumStoredWidths += storedWidths[i];
    }

    size_t const outputEltWidthBits = (size_t)outputEltWidth * 8;

    // Validate sum doesn't exceed output width
    ZL_RET_R_IF_GT(
            corruption,
            sumStoredWidths,
            outputEltWidthBits,
            "bitSplit: sum of stored widths exceeds output element width");

    // Determine coverage based on number of input streams:
    // - N inputs (== nbStoredWidths) → partial coverage, upper bits are zero
    // - N+1 inputs → full coverage, last width = outputEltWidthBits - sum
    size_t const lastWidth = outputEltWidthBits - sumStoredWidths;

    uint8_t bitWidths_local[65];

    // Initialize like partial coverage
    const uint8_t* bitWidths = storedWidths;
    size_t nbWidths          = nbStoredWidths;

    if (nbVariableSrcs == nbStoredWidths) {
        // Partial coverage: use stored widths as-is
    } else if (nbVariableSrcs == nbStoredWidths + 1) {
        // Full coverage: add computed last width
        ZL_ASSERT_LE(nbStoredWidths, 64); // already checked via sumStoredWidths
        ZL_memcpy(bitWidths_local, storedWidths, nbStoredWidths);
        bitWidths_local[nbStoredWidths] = (uint8_t)lastWidth;

        bitWidths = bitWidths_local;
        nbWidths  = nbStoredWidths + 1;
    } else {
        ZL_RET_R_IF_NOT(corruption, 0, "bitSplit: input stream count mismatch");
    }

    // Validate: must have at least one width
    ZL_RET_R_IF_EQ(corruption, nbWidths, 0, "bitSplit: no bit widths present");

    // Validate all input streams and get their pointers
    size_t nbElts = 0;
    const void* inputPtrs[64];
    size_t inputWidths[64];
    ZL_ASSERT_LE(nbWidths, 64);

    for (size_t i = 0; i < nbWidths; i++) {
        const ZL_Input* in = variableSrcs[i];
        ZL_ASSERT_NN(in);
        ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);

        // Verify element count consistency
        size_t const streamNbElts = ZL_Input_numElts(in);
        if (i == 0) {
            nbElts = streamNbElts;
        } else {
            ZL_RET_R_IF_NE(
                    corruption,
                    streamNbElts,
                    nbElts,
                    "bitSplit: all input streams must have same element count");
        }

        // Verify element width matches expected
        size_t const expectedWidth = ZS_bitSplit_outputEltWidth(bitWidths[i]);
        ZL_RET_R_IF_NE(
                corruption,
                ZL_Input_eltWidth(in),
                expectedWidth,
                "bitSplit: input stream element width mismatch");

        inputPtrs[i]   = ZL_Input_ptr(in);
        inputWidths[i] = expectedWidth;
    }

    // Create output stream
    ZL_Output* const out =
            ZL_Decoder_create1OutStream(dictx, nbElts, outputEltWidth);
    ZL_RET_R_IF_NULL(allocation, out);

    // Kernel owns the hot loop - single call processes all elements
    ZS_bitSplitDecode(
            ZL_Output_ptr(out),
            outputEltWidth,
            nbElts,
            inputPtrs,
            inputWidths,
            bitWidths,
            nbWidths);

    ZL_RET_R_IF_ERR(ZL_Output_commit(out, nbElts));
    return ZL_returnValue(1);
}
