// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/sentinel/encode_sentinel_binding.h"

#include "openzl/codecs/sentinel/encode_sentinel_kernel.h"
#include "openzl/codecs/zl_sentinel.h"
#include "openzl/common/assertion.h"
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_ctransform.h"

/// Send the codec header. Empty if sentinel == max value for valWidth,
/// otherwise varint-encoded sentinel.
static void
sendSentinelHeader(ZL_Encoder* eictx, uint64_t sentinel, size_t valWidth)
{
    if (sentinel == ZL_maxValueForWidth(valWidth)) {
        // Default sentinel: empty header
        return;
    }
    uint8_t buf[ZL_VARINT_LENGTH_64];
    const size_t size = ZL_varintEncode(sentinel, buf);
    ZL_Encoder_sendCodecHeader(eictx, buf, size);
}

ZL_Report
EI_sentinel_byte(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);

    const size_t eltWidth = ZL_Input_eltWidth(in);
    const size_t numElts  = ZL_Input_numElts(in);

    ZL_ERR_IF_EQ(
            eltWidth,
            1,
            node_invalid_input,
            "sentinel_byte does not accept 1-byte inputs");

    /* Output 0: values at 1 byte per element */
    ZL_Output* const valuesOut =
            ZL_Encoder_createTypedStream(eictx, 0, numElts, 1);
    ZL_ERR_IF_NULL(valuesOut, allocation);

    /* Output 1: exceptions at original width (worst case: all elements) */
    ZL_Output* const exceptionsOut =
            ZL_Encoder_createTypedStream(eictx, 1, numElts, eltWidth);
    ZL_ERR_IF_NULL(exceptionsOut, allocation);

    const size_t numExceptions = ZL_sentinelByteEncode(
            (uint8_t*)ZL_Output_ptr(valuesOut),
            ZL_Output_ptr(exceptionsOut),
            ZL_Input_ptr(in),
            numElts,
            eltWidth);

    ZL_ERR_IF_ERR(ZL_Output_commit(valuesOut, numElts));
    ZL_ERR_IF_ERR(ZL_Output_commit(exceptionsOut, numExceptions));

    return ZL_returnSuccess();
}

ZL_Report EI_sentinel(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);

    const size_t eltWidth = ZL_Input_eltWidth(in);
    const size_t numElts  = ZL_Input_numElts(in);

    /* Read exception indices from ref param */
    const ZL_RefParam indicesParam =
            ZL_Encoder_getLocalParam(eictx, ZL_SENTINEL_INDICES_PID);
    ZL_ERR_IF_NE(
            indicesParam.paramId,
            ZL_SENTINEL_INDICES_PID,
            nodeParameter_invalid,
            "Missing indices");
    const size_t* const exceptionIndices = (const size_t*)indicesParam.paramRef;
    const size_t numExceptions = indicesParam.paramSize / sizeof(size_t);

    /* Read sentinel from ref param (optional, defaults to max for width) */
    const uint64_t maxVal = ZL_maxValueForWidth(eltWidth);
    uint64_t sentinel     = maxVal;
    const ZL_RefParam sentinelParam =
            ZL_Encoder_getLocalParam(eictx, ZL_SENTINEL_VALUE_PID);
    if (sentinelParam.paramRef != NULL) {
        ZL_ERR_IF_NE(
                sentinelParam.paramSize,
                sizeof(uint64_t),
                nodeParameter_invalid,
                "Sentinel value must be 64 bits");
        memcpy(&sentinel, sentinelParam.paramRef, sizeof(uint64_t));
    }
    // Mask the sentinel to the width of the input to allow for signed types
    // passed as uint64_t.
    sentinel &= maxVal;

    /* Output 0: values at same width */
    ZL_Output* const valuesOut =
            ZL_Encoder_createTypedStream(eictx, 0, numElts, eltWidth);
    ZL_ERR_IF_NULL(valuesOut, allocation);

    /* Output 1: exceptions at same width */
    ZL_Output* const exceptionsOut =
            ZL_Encoder_createTypedStream(eictx, 1, numExceptions, eltWidth);
    ZL_ERR_IF_NULL(exceptionsOut, allocation);

    const int rc = ZL_sentinelEncode(
            ZL_Output_ptr(valuesOut),
            ZL_Output_ptr(exceptionsOut),
            ZL_Input_ptr(in),
            numElts,
            eltWidth,
            exceptionIndices,
            numExceptions,
            sentinel);
    ZL_ERR_IF_NE(rc, 0, GENERIC, "Sentinel encode validation failed");

    sendSentinelHeader(eictx, sentinel, eltWidth);

    ZL_ERR_IF_ERR(ZL_Output_commit(valuesOut, numElts));
    ZL_ERR_IF_ERR(ZL_Output_commit(exceptionsOut, numExceptions));

    return ZL_returnSuccess();
}

ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runSentinelNode(
        ZL_Edge* input,
        const size_t* exceptionIndices,
        size_t numExceptions,
        uint64_t sentinel)
{
    const ZL_RefParam refParams[2] = {
        { ZL_SENTINEL_INDICES_PID,
          exceptionIndices,
          numExceptions * sizeof(size_t) },
        { ZL_SENTINEL_VALUE_PID, &sentinel, sizeof(uint64_t) },
    };
    const ZL_LocalParams lp = {
        .refParams = { refParams, 2 },
    };
    return ZL_Edge_runNode_withParams(input, ZL_NODE_SENTINEL_NUM, &lp);
}
