// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/dedup/encode_dedup_binding.h"

#include <string.h> // memcmp
#include "openzl/common/assertion.h"
#include "openzl/compress/enc_interface.h" // ENC_refTypedStream
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"

static ZL_Report EI_dedup_num_internal(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns,
        int inputsIdentical)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    // Input sanitization
    ZL_ASSERT_GE(nbIns, 1);
    ZL_ASSERT_NN(ins);
    ZL_ASSERT_NN(ins[0]);
    size_t const eltCount  = ZL_Input_numElts(ins[0]);
    size_t const eltWidth  = ZL_Input_eltWidth(ins[0]);
    size_t const totalSize = eltCount * eltWidth;

    for (size_t n = 1; n < nbIns; n++) {
        ZL_ASSERT_NN(ins[n]);
        ZL_ASSERT_EQ(ZL_Input_type(ins[n]), ZL_Type_numeric);

        if (inputsIdentical) {
            // Inputs should be all identical
            ZL_ASSERT_EQ(ZL_Input_eltWidth(ins[n]), eltWidth);
            ZL_ASSERT_EQ(ZL_Input_numElts(ins[n]), eltCount);
            ZL_ASSERT_EQ(
                    memcmp(ZL_Input_ptr(ins[n]),
                           ZL_Input_ptr(ins[0]),
                           totalSize),
                    0);
        } else {
            // Actively check that inputs are indeed all identical
            ZL_ERR_IF_NE(
                    ZL_Input_eltWidth(ins[n]), eltWidth, node_invalid_input);
            ZL_ERR_IF_NE(
                    ZL_Input_numElts(ins[n]), eltCount, node_invalid_input);
            int const isDifferent = memcmp(
                    ZL_Input_ptr(ins[n]), ZL_Input_ptr(ins[0]), totalSize);
            ZL_ERR_IF(isDifferent, node_invalid_input);
        }
    }

    ZL_Output* const out =
            ENC_refTypedStream(eictx, 0, eltWidth, eltCount, ins[0], 0);
    ZL_ERR_IF_NULL(out, allocation);

    return ZL_returnSuccess();
}

ZL_Report EI_dedup_num(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_IntParam tp =
            ZL_Encoder_getLocalIntParam(eictx, ZL_DEDUP_TRUST_IDENTICAL);
    int const inputsTrusted =
            (tp.paramId == ZL_DEDUP_TRUST_IDENTICAL && tp.paramValue == 1);
    return EI_dedup_num_internal(eictx, ins, nbIns, inputsTrusted);
}

ZL_Report
EI_dedup_num_trusted(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    return EI_dedup_num_internal(eictx, ins, nbIns, 1 /* trusted */);
}
