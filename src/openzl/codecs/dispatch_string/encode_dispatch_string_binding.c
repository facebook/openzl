// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/dispatch_string/encode_dispatch_string_binding.h"

#include <string.h> // memcpy

#include "openzl/codecs/dispatch_string/common_dispatch_string.h"
#include "openzl/codecs/dispatch_string/encode_dispatch_string_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_graph_api.h" // ZL_Edge_runNode_withParams

size_t ZL_DispatchString_maxDispatches(void)
{
    return ZL_DISPATCH_STRING_MAX_DISPATCHES;
}

ZL_Report
EI_dispatch_string(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_string);
    const size_t nbElts = ZL_Input_numElts(in);

    // get local params
    const int nbOutputsInt = ZL_Encoder_getLocalIntParam(
                                     eictx, ZL_DISPATCH_STRING_NUM_OUTPUTS_PID)
                                     .paramValue;
    const size_t nbOutputs = (size_t)nbOutputsInt;
    ZL_ERR_IF_GT(
            nbOutputs,
            ZL_DISPATCH_STRING_MAX_DISPATCHES,
            streamParameter_invalid,
            "dispatch_string: invalid number of outputs");
    ZL_ERR_IF(
            nbElts > 0 && nbOutputs == 0,
            streamParameter_invalid,
            "dispatch_string: ill-formed degenerate case (%u, %i)",
            nbElts,
            nbOutputsInt);

    const uint16_t* indices = (const uint16_t*)ZL_Encoder_getLocalParam(
                                      eictx, ZL_DISPATCH_STRING_INDICES_PID)
                                      .paramRef;
    ZL_ERR_IF_NULL(
            indices,
            streamParameter_invalid,
            "dispatch_string: indices pointer is null");

    // validate indices stream and calculate output sizes
    unsigned int indicesValid  = 1;
    const uint32_t* srcStrLens = ZL_Input_stringLens(in);
    for (size_t i = 0; i < nbElts; ++i) {
        indicesValid &= (indices[i] < nbOutputs);
    }

    ZL_ERR_IF_NOT(
            indicesValid == 1,
            streamParameter_invalid,
            "Dispatch index out of bounds. Expected all to be in range [0,%u)",
            nbOutputs);

    size_t* outputSizes =
            ZL_Encoder_getScratchSpace(eictx, nbOutputs * sizeof(size_t));
    ZL_ERR_IF_NULL(outputSizes, allocation);
    memset(outputSizes, 0, nbOutputs * sizeof(size_t));
    for (size_t i = 0; i < nbElts; ++i) {
        outputSizes[indices[i]] += srcStrLens[i];
    }

    ZL_DLOG(TRANSFORM,
            "EI_dispatch_string: splitting %u strings into %i outputs",
            nbElts,
            nbOutputs);

    // create streams for outputs
    ZL_Output* indices_out =
            ZL_Encoder_createTypedStream(eictx, 0, nbElts, sizeof(uint16_t));
    ZL_ERR_IF_NULL(indices_out, allocation);

    // allocate space for output streams and raw handles
    ZL_Output** outs =
            ZL_Encoder_getScratchSpace(eictx, nbOutputs * sizeof(ZL_Output*));
    ZL_ERR_IF_NULL(outs, allocation);

    void** dstBuffers =
            ZL_Encoder_getScratchSpace(eictx, nbOutputs * sizeof(void*));
    ZL_ERR_IF_NULL(dstBuffers, allocation);

    uint32_t** dstStrLens =
            ZL_Encoder_getScratchSpace(eictx, nbOutputs * sizeof(uint32_t*));
    ZL_ERR_IF_NULL(dstStrLens, allocation);

    size_t* dstNbStrs =
            ZL_Encoder_getScratchSpace(eictx, nbOutputs * sizeof(size_t));
    ZL_ERR_IF_NULL(dstNbStrs, allocation);
    if (nbElts > 0) {
        for (int i = 0; i < (int)nbOutputs; ++i) {
            ZL_Output* out = ZL_Encoder_createStringStream(
                    eictx,
                    1,
                    nbElts,
                    outputSizes[i] + ZL_DISPATCH_STRING_BLK_SIZE);
            ZL_ERR_IF_NULL(out, allocation);
            outs[i]       = out;
            dstBuffers[i] = ZL_Output_ptr(out);
            dstStrLens[i] = ZL_Output_stringLens(out);
        }

        ZL_DispatchString_encode16(
                (uint16_t)nbOutputs,
                dstBuffers,
                dstStrLens,
                dstNbStrs,
                ZL_Input_ptr(in),
                ZL_Input_stringLens(in),
                nbElts,
                indices);

        for (size_t i = 0u; i < nbOutputs; ++i) {
            ZL_ERR_IF_ERR(ZL_Output_commit(outs[i], dstNbStrs[i]));
        }
    }

    // additionally write the indices as an output stream
    if (nbElts > 0) {
        memcpy(ZL_Output_ptr(indices_out), indices, nbElts * sizeof(uint16_t));
    }
    ZL_ERR_IF_ERR(ZL_Output_commit(indices_out, nbElts));

    return ZL_returnSuccess();
}

ZL_NodeID ZL_Compressor_registerDispatchStringNode(
        ZL_Compressor* cgraph,
        int nbOutputsParam,
        const uint16_t* dispatchIndicesParam)
{
    ZL_LocalParams localParams = {
        .intParams = ZL_INTPARAMS(
                { ZL_DISPATCH_STRING_NUM_OUTPUTS_PID, nbOutputsParam }),
        .refParams = ZL_REFPARAMS(
                { ZL_DISPATCH_STRING_INDICES_PID, dispatchIndicesParam }),
    };
    return ZL_Compressor_cloneNode(
            cgraph, ZL_NODE_DISPATCH_STRING, &localParams);
}

ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runDispatchStringNode(
        ZL_Edge* sctx,
        int nbOutputs,
        const uint16_t* indices)
{
    ZL_RET_T_IF(
            ZL_EdgeList,
            nodeParameter_invalid,
            nbOutputs < 0 || nbOutputs > ZL_DISPATCH_STRING_MAX_DISPATCHES,
            "dispatch_string: invalid number of outputs (%i)",
            nbOutputs);

    const ZL_LocalParams params = {
        .intParams =
                ZL_INTPARAMS({ ZL_DISPATCH_STRING_NUM_OUTPUTS_PID, nbOutputs }),
        .refParams = ZL_REFPARAMS({ ZL_DISPATCH_STRING_INDICES_PID, indices }),
    };

    return ZL_Edge_runNode_withParams(sctx, ZL_NODE_DISPATCH_STRING, &params);
}
