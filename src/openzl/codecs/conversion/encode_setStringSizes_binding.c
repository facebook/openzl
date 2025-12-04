// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <stdint.h>
#include "openzl/codecs/conversion/encode_conversion_binding.h"
#include "openzl/common/assertion.h"
#include "openzl/common/logging.h"            // ZL_DLOG
#include "openzl/compress/enc_interface.h"    // ENC_*
#include "openzl/compress/private_nodes.h"    // ZL_NODE_SETSTRINGLENS
#include "openzl/shared/mem.h"                // ZL_memcpy
#include "openzl/shared/numeric_operations.h" // NUMOP_*
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"      // ZS2_Data_*
#include "openzl/zl_graph_api.h" // ZL_Edge_runNode_withParams
#include "openzl/zl_localParams.h"
#include "openzl/zl_public_nodes.h"

/* ----- Set String Sizes --------- */

struct ZL_SetStringLensState_s {
    ZL_Encoder* eictx;
};

typedef struct {
    ZL_SetStringLensParserFn f;
    void const* opaque;
} SetStringLens_Parser_s;

static SetStringLens_Parser_s const* getExtParser(ZL_Encoder const* eictx)
{
    ZL_CopyParam const gpParsef =
            ZL_Encoder_getLocalCopyParam(eictx, ZL_SETSTRINGLENS_PARSINGF_PID);

    if (gpParsef.paramId != ZL_SETSTRINGLENS_PARSINGF_PID) {
        return NULL;
    }

    return (const SetStringLens_Parser_s*)gpParsef.paramPtr;
}

static ZL_SetStringLensInstructions getStringLens(
        ZL_Encoder* eictx,
        const ZL_Input* in)
{
    ZL_DLOG(SEQ, "getStringLens()");

    // Receive lengths as a reference to an array
    ZL_RefParam stringLensParam =
            ZL_Encoder_getLocalParam(eictx, ZL_SETSTRINGLENS_ARRAY_PID);
    if (stringLensParam.paramId == ZL_SETSTRINGLENS_ARRAY_PID) {
        return (ZL_SetStringLensInstructions){
            .nbStrings  = (size_t)stringLensParam.paramSize / sizeof(uint32_t),
            .stringLens = stringLensParam.paramRef,
        };
    }

    // Generate lengths via a parser function
    const SetStringLens_Parser_s* s = getExtParser(eictx);
    if (s == NULL) {
        ZL_DLOG(ERROR, "setStringLens parser not provided");
        return (ZL_SetStringLensInstructions){ NULL, (size_t)(-1) };
    }

    ZL_SetStringLensParserFn f       = s->f;
    ZL_SetStringLensState allocState = { eictx };
    return f(&allocState, in);
}

ZL_Report
EI_setStringLens(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(in);
    size_t const inputSize = ZL_Input_numElts(in);
    ZL_DLOG(BLOCK, "EI_setStringLens (in:%zu bytes)", inputSize);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);
    ZL_ASSERT_NN(eictx);

    ZL_SetStringLensInstructions sfsi = getStringLens(eictx, in);
    const size_t nbStrings            = sfsi.nbStrings;
    const uint32_t* const stringLens  = sfsi.stringLens;

    ZL_RET_R_IF(nodeParameter_invalid, nbStrings && stringLens == NULL);

    ZL_DLOG(BLOCK,
            "EI_setStringLens: converting %zu bytes into %zu strings",
            inputSize,
            nbStrings);

    // Here : check parser's output
    uint64_t const parserTotalSize = NUMOP_sumArray32(stringLens, nbStrings);
    ZL_RET_R_IF_NE(
            nodeParameter_invalidValue,
            parserTotalSize,
            (uint64_t)inputSize,
            "EI_setStringLens: the external parser provides invalid total size");

    ZL_Output* const out = ENC_refTypedStream(eictx, 0, 1, inputSize, in, 0);
    ZL_RET_R_IF_NULL(allocation, out);

    uint32_t* const fs = ZL_Output_reserveStringLens(out, nbStrings);
    ZL_RET_R_IF_NULL(allocation, fs);

    ZL_memcpy(fs, stringLens, nbStrings * sizeof(uint32_t));

    ZL_RET_R_IF_ERR(ZL_Output_commit(out, nbStrings));

    return ZL_returnSuccess();
}

void* ZL_SetStringLensState_malloc(ZL_SetStringLensState* state, size_t size)
{
    return ZL_Encoder_getScratchSpace(state->eictx, size);
}

const void* ZL_SetStringLensState_getOpaquePtr(
        const ZL_SetStringLensState* state)
{
    const SetStringLens_Parser_s* s = getExtParser(state->eictx);
    return s->opaque;
}

ZL_NodeID ZL_Compressor_registerConvertSerialToStringNode(
        ZL_Compressor* cgraph,
        ZL_SetStringLensParserFn f,
        void const* opaque)
{
    ZL_DLOG(SEQ, "ZL_Compressor_registerConvertSerialToStringNode");
    SetStringLens_Parser_s const s = { f, opaque };
    ZL_CopyParam const ssp         = { .paramId   = ZL_SETSTRINGLENS_PARSINGF_PID,
                                       .paramPtr  = &s,
                                       .paramSize = sizeof(s) };

    ZL_LocalCopyParams const lgp = { &ssp, 1 };
    ZL_LocalParams const lParams = { .copyParams = lgp };
    return ZL_Compressor_cloneNode(cgraph, ZL_NODE_SETSTRINGLENS, &lParams);
}

ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runConvertSerialToStringNode(
        ZL_Edge* sctx,
        const uint32_t* stringLens,
        size_t nbString)
{
    if (nbString)
        ZL_ASSERT_NN(stringLens);

    const ZL_LocalParams params = {
        .refParams = ZL_REFPARAMS(
                { ZL_SETSTRINGLENS_ARRAY_PID,
                  stringLens,
                  nbString * sizeof(uint32_t) }),
    };

    return ZL_Edge_runNode_withParams(sctx, ZL_NODE_SETSTRINGLENS, &params);
}
