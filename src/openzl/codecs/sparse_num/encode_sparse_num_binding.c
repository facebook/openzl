// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/sparse_num/encode_sparse_num_binding.h"

#include "openzl/codecs/sparse_num/encode_sparse_num_kernel.h"
#include "openzl/codecs/zl_sparse_num.h"
#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/mem.h"

#include <stdbool.h>
#include <stdint.h>

static bool ZL_sparseNumDominantFitsWidth(uint64_t dominant, size_t width)
{
    switch (width) {
        case 1:
            return dominant <= UINT8_MAX;
        case 2:
            return dominant <= UINT16_MAX;
        case 4:
            return dominant <= UINT32_MAX;
        case 8:
            return true;
        default:
            ZL_ASSERT_FAIL("Unsupported sparse_num value width");
            return false;
    }
}

static size_t ZL_sparseNumWriteDominantHeader(
        uint8_t header[sizeof(uint64_t)],
        uint64_t dominant)
{
    size_t const size = (size_t)ZL_highbit64(dominant) / 8 + 1;
    ZL_writeLE64_N(header, dominant, size);
    return size;
}

static ZL_Report ZL_sparseNumEncoderDominant(
        ZL_Encoder* eictx,
        const ZL_Input* in,
        size_t valueWidth,
        uint64_t* dominant)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_NN(dominant);

    ZL_RefParam const dominantParam =
            ZL_Encoder_getLocalParam(eictx, ZL_SPARSE_NUM_DOMINANT_VALUE_PID);
    if (dominantParam.paramRef != NULL) {
        ZL_ERR_IF_NE(
                dominantParam.paramSize,
                sizeof(uint64_t),
                graphParameter_invalid,
                "sparse_num dominant parameter must be uint64_t");
        uint64_t const explicitDominant =
                *(const uint64_t*)dominantParam.paramRef;
        ZL_ERR_IF_NOT(
                ZL_sparseNumDominantFitsWidth(explicitDominant, valueWidth),
                node_invalid_input,
                "sparse_num dominant value does not fit input width");
        if (ZL_Input_numElts(in) == 0) {
            *dominant = 0;
            return ZL_returnSuccess();
        }
        *dominant = explicitDominant;
        return ZL_returnSuccess();
    }

    *dominant = ZL_sparseNumSelectDominant(
            ZL_Input_ptr(in), ZL_Input_numElts(in), valueWidth);
    return ZL_returnSuccess();
}

static ZL_Report ZL_sparseNumEncodeSelectedDominant(
        ZL_Encoder* eictx,
        const ZL_Input* in,
        size_t valueWidth,
        uint64_t dominant)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    bool const nonZeroDominant = dominant != 0;

    if (nonZeroDominant) {
        uint8_t header[sizeof(uint64_t)];
        size_t const headerSize =
                ZL_sparseNumWriteDominantHeader(header, dominant);
        ZL_ASSERT_LE(headerSize, valueWidth);
        ZL_Encoder_sendCodecHeader(eictx, header, headerSize);
    }

    ZL_SparseNumEncodeInfo const info = ZL_sparseNumComputeEncodeInfo(
            ZL_Input_ptr(in), ZL_Input_numElts(in), valueWidth, dominant);

    ZL_Output* const distances = ZL_Encoder_createTypedStream(
            eictx, 0, info.numDistances, info.distanceWidth);
    ZL_ERR_IF_NULL(distances, allocation);
    ZL_Output* const values =
            ZL_Encoder_createTypedStream(eictx, 1, info.numValues, valueWidth);
    ZL_ERR_IF_NULL(values, allocation);

    ZL_sparseNumEncode(
            ZL_Output_ptr(distances),
            ZL_Output_ptr(values),
            ZL_Input_ptr(in),
            ZL_Input_numElts(in),
            valueWidth,
            info.distanceWidth,
            dominant);

    ZL_ERR_IF_ERR(ZL_Output_commit(distances, info.numDistances));
    ZL_ERR_IF_ERR(ZL_Output_commit(values, info.numValues));

    return ZL_returnSuccess();
}

static ZL_Report ZL_sparseNumEncodeNode(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns,
        bool autoDominant)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);

    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);

    size_t const valueWidth = ZL_Input_eltWidth(in);
    ZL_ERR_IF_NOT(
            ZL_sparseNumValidValueWidth(valueWidth),
            node_invalid_input,
            "sparse_num expects numeric width of 1, 2, 4 or 8 bytes");

    uint64_t dominant = 0;
    if (autoDominant) {
        ZL_ERR_IF_ERR(
                ZL_sparseNumEncoderDominant(eictx, in, valueWidth, &dominant));
    }

    return ZL_sparseNumEncodeSelectedDominant(eictx, in, valueWidth, dominant);
}

ZL_Report EI_sparse_num(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    return ZL_sparseNumEncodeNode(eictx, ins, nbIns, false);
}

ZL_Report
EI_sparse_num_auto(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    return ZL_sparseNumEncodeNode(eictx, ins, nbIns, true);
}
