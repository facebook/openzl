// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <string.h>

#include "openzl/codecs/bitSplit/decode_bitSplit_binding.h"
#include "openzl/codecs/bitSplit/decode_bitSplit_kernel.h"
#include "openzl/codecs/bitSplit/encode_bitSplit_kernel.h" // ZS_bitSplit_outputEltWidth
#include "openzl/common/assertion.h"
#include "openzl/decompress/dictx.h"

ZL_Report DI_bitSplit(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs)
{
    ZL_ASSERT_NN(dictx);
    (void)compulsorySrcs;
    (void)nbCompulsorySrcs;

    // Get parameters from codec header
    // Header format: [inputEltWidth (1 byte)] [stored widths...]
    ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dictx);

    // Validate: header must have at least inputEltWidth byte
    ZL_RET_R_IF_LT(
            corruption,
            header.size,
            1,
            "bitSplit: header must contain inputEltWidth");

    // Parse header
    uint8_t const inputEltWidth = ((const uint8_t*)header.start)[0];
    size_t const nbStoredWidths = header.size - 1;
    const uint8_t* storedWidths = (const uint8_t*)header.start + 1;

    // Validate inputEltWidth
    ZL_RET_R_IF_NOT(
            corruption,
            inputEltWidth == 1 || inputEltWidth == 2 || inputEltWidth == 4
                    || inputEltWidth == 8,
            "bitSplit: invalid inputEltWidth in header");

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

    size_t const inputEltWidthBits = (size_t)inputEltWidth * 8;

    // Validate sum doesn't exceed input width
    ZL_RET_R_IF_GT(
            corruption,
            sumStoredWidths,
            inputEltWidthBits,
            "bitSplit: sum of stored widths exceeds input element width");

    size_t const lastWidth = inputEltWidthBits - sumStoredWidths;

    // Build complete widths array
    uint8_t allWidths[65];
    memcpy(allWidths, storedWidths, nbStoredWidths);
    size_t nbWidths = nbStoredWidths;
    if (lastWidth > 0) {
        allWidths[nbWidths++] = (uint8_t)lastWidth;
    }

    // Validate: must have at least one width
    ZL_RET_R_IF_EQ(corruption, nbWidths, 0, "bitSplit: no bit widths present");

    // Validate number of input streams
    // For partial coverage, we may have fewer input streams (the last is
    // implicit zeros)
    ZL_RET_R_IF_GT(
            corruption,
            nbVariableSrcs,
            nbWidths,
            "bitSplit: too many input streams");
    ZL_RET_R_IF_LT(
            corruption,
            nbVariableSrcs,
            nbStoredWidths,
            "bitSplit: not enough input streams");

    // Validate all input streams and get their pointers
    size_t nbElts = 0;
    const void* inputPtrs[64];
    size_t inputWidths[64];
    ZL_ASSERT_LE(nbWidths, 64);

    for (size_t i = 0; i < nbVariableSrcs; i++) {
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
        size_t const expectedWidth = ZS_bitSplit_outputEltWidth(allWidths[i]);
        ZL_RET_R_IF_NE(
                corruption,
                ZL_Input_eltWidth(in),
                expectedWidth,
                "bitSplit: input stream element width mismatch");

        inputPtrs[i]   = ZL_Input_ptr(in);
        inputWidths[i] = expectedWidth;
    }

    // Handle missing input streams (implicit zeros for partial coverage)
    // If nbVariableSrcs < nbWidths, the missing stream contributes zeros
    for (size_t i = nbVariableSrcs; i < nbWidths; i++) {
        inputPtrs[i]   = NULL; // Will be treated as zeros
        inputWidths[i] = ZS_bitSplit_outputEltWidth(allWidths[i]);
    }

    // Use inputEltWidth from header for output (not computed from sum)
    size_t const outputEltWidth = inputEltWidth;

    // Create output stream
    ZL_Output* const out =
            ZL_Decoder_create1OutStream(dictx, nbElts, outputEltWidth);
    ZL_RET_R_IF_NULL(allocation, out);

    void* dstPtr = ZL_Output_ptr(out);

    // Reconstruct each element
    for (size_t e = 0; e < nbElts; e++) {
        uint64_t value = 0;
        size_t bitPos  = 0;

        for (size_t i = 0; i < nbWidths; i++) {
            uint64_t part = 0;
            if (inputPtrs[i] != NULL) {
                // Read from actual input stream
                switch (inputWidths[i]) {
                    case 1:
                        part = ((const uint8_t*)inputPtrs[i])[e];
                        break;
                    case 2:
                        part = ((const uint16_t*)inputPtrs[i])[e];
                        break;
                    case 4:
                        part = ((const uint32_t*)inputPtrs[i])[e];
                        break;
                    case 8:
                        part = ((const uint64_t*)inputPtrs[i])[e];
                        break;
                }
            }
            // else: inputPtrs[i] == NULL means implicit zeros, part stays 0

            // Mask to the bit width
            uint64_t const mask =
                    (allWidths[i] == 64) ? ~0ULL : ((1ULL << allWidths[i]) - 1);
            part &= mask;

            // Place in output value
            value |= (part << bitPos);
            bitPos += allWidths[i];
        }

        switch (outputEltWidth) {
            case 1:
                ((uint8_t*)dstPtr)[e] = (uint8_t)value;
                break;
            case 2:
                ((uint16_t*)dstPtr)[e] = (uint16_t)value;
                break;
            case 4:
                ((uint32_t*)dstPtr)[e] = (uint32_t)value;
                break;
            case 8:
                ((uint64_t*)dstPtr)[e] = value;
                break;
        }
    }

    ZL_RET_R_IF_ERR(ZL_Output_commit(out, nbElts));
    return ZL_returnValue(1);
}
