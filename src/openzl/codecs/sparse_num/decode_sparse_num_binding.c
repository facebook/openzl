// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/sparse_num/decode_sparse_num_binding.h"

#include "openzl/codecs/sparse_num/decode_sparse_num_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/overflow.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Decoder binding responsibilities:
 * - validate the wire-level header and stream metadata,
 * - translate the header into a dominant symbol,
 * - compute and allocate the exact output size,
 * - then call the small dependency-light kernel.
 */
ZL_Report DI_sparse_num(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ASSERT_NN(dictx);
    ZL_ASSERT_NN(ins);

    const ZL_Input* const distances = ins[0];
    const ZL_Input* const values    = ins[1];
    ZL_ASSERT_NN(distances);
    ZL_ASSERT_NN(values);

    ZL_ASSERT_EQ(ZL_Input_type(distances), ZL_Type_numeric);
    ZL_ASSERT_EQ(ZL_Input_type(values), ZL_Type_numeric);

    size_t const distanceWidth = ZL_Input_eltWidth(distances);
    ZL_ERR_IF_NOT(
            ZL_sparseNumValidDistanceWidth(distanceWidth),
            corruption,
            "sparse_num distance width must be 1, 2 or 4 bytes");

    size_t const valueWidth = ZL_Input_eltWidth(values);
    ZL_ERR_IF_NOT(
            ZL_sparseNumValidValueWidth(valueWidth),
            corruption,
            "sparse_num value width must be 1, 2, 4 or 8 bytes");

    ZL_RBuffer const header = ZL_Decoder_getCodecHeader(dictx);
    ZL_ERR_IF_GT(
            header.size,
            valueWidth,
            corruption,
            "sparse_num codec header must be no larger than the value width");

    size_t const numDistances = ZL_Input_numElts(distances);
    size_t const numValues    = ZL_Input_numElts(values);

    /*
     * Empty header is the canonical zero-dominant mode: all values are
     * literals, and the kernel can stay on the zero-specialized path.
     * Non-empty headers encode the dominant symbol directly as little-endian
     * bytes; missing high bytes are implicit zero.
     */
    uint64_t dominant = 0;
    if (header.size > 0) {
        if (header.start == NULL) {
            ZL_ERR(corruption);
        }
        dominant = ZL_readLE64_N(header.start, header.size);
    }

    bool const validCounts = numDistances == numValues
            || (numValues < SIZE_MAX && numDistances == numValues + 1);
    ZL_ERR_IF_NOT(
            validCounts,
            corruption,
            "sparse_num distance count must be literal count or literal count + 1");

    size_t const outputCount = ZL_sparseNumDecodeOutputCount(
            ZL_Input_ptr(distances), numDistances, distanceWidth, numValues);
    ZL_ERR_IF_EQ(
            outputCount,
            ZL_SPARSE_NUM_DECODE_OUTPUT_COUNT_ERROR,
            integerOverflow,
            "sparse_num output element count overflows size_t");
    size_t outputSize;
    ZL_ERR_IF(
            ZL_overflowMulST(outputCount, valueWidth, &outputSize),
            integerOverflow,
            "sparse_num output byte size overflows size_t");

    ZL_Output* const out =
            ZL_Decoder_create1OutStream(dictx, outputCount, valueWidth);
    ZL_ERR_IF_NULL(out, allocation);

    ZL_sparseNumDecode(
            ZL_Output_ptr(out),
            outputSize,
            ZL_Input_ptr(distances),
            numDistances,
            distanceWidth,
            dominant,
            ZL_Input_ptr(values),
            numValues,
            valueWidth);

    ZL_ERR_IF_ERR(ZL_Output_commit(out, outputCount));
    return ZL_returnSuccess();
}
