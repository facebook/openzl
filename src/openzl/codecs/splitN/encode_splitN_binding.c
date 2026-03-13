// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/splitN/encode_splitN_binding.h"

#include <string.h>

#include "openzl/common/assertion.h"
#include "openzl/common/logging.h" // ZL_DLOG
#include "openzl/compress/enc_interface.h" // eictx, ENC_refTypedStream, ZL_Encoder_getScratchSpace
#include "openzl/compress/private_nodes.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_data.h"      // ZS2_Data_*
#include "openzl/zl_graph_api.h" // ZL_Edge_runNode_withParams
#include "openzl/zl_selector.h"  // ZL_SelectorDesc

// could be any (non trivial) arbitrary value
#define ZL_SPLITN_SEGMENTSIZES_PID 323
#define ZL_SPLITN_PARSINGF_PID 436
#define ZL_SPLITN_PARSING_OPAQUE_PTR_PID 437

struct ZL_SplitState_s {
    ZL_Encoder* eictx;
};

typedef struct {
    ZL_SplitParserFn f;
    void const* opaque;
} SplitN_ExtParser_s;

ZL_RESULT_DECLARE_TYPE(ZL_SplitInstructions);

/**
 * Gets the external parser by checking through the local parameters for a
 * matching refParam. The reason why we cannot directly use
 * ZL_Encoder_getLocalCopyParam is function pointers are not serializable,
 * therefore we only check for refParams to ensure a deserialized compressor
 * cannot contain this parameter.
 */
static SplitN_ExtParser_s getExtParser(ZL_Encoder const* eictx)
{
    SplitN_ExtParser_s extParser = { NULL, NULL };
    const ZL_LocalParams* lp     = ZL_Encoder_getLocalParams(eictx);
    for (size_t n = 0; n < lp->refParams.nbRefParams; n++) {
        if (lp->refParams.refParams[n].paramId == ZL_SPLITN_PARSINGF_PID) {
            // We use memcpy here to avoid compiler warnings about casting a
            // const pointer to a function pointer.
            memcpy(&extParser.f,
                   &lp->refParams.refParams[n].paramRef,
                   sizeof(extParser.f));
        }
        if (lp->refParams.refParams[n].paramId
            == ZL_SPLITN_PARSING_OPAQUE_PTR_PID) {
            extParser.opaque = lp->refParams.refParams[n].paramRef;
        }
    }
    return extParser;
}

static ZL_RESULT_OF(ZL_SplitInstructions)
        getSplitInstructions(ZL_Encoder* eictx, const ZL_Input* in)
{
    ZL_DLOG(SEQ, "getSplitInstructions()");

    ZL_SplitState allocState = { eictx };

    // Priority 1 : check for external parsing function
    SplitN_ExtParser_s extParser = getExtParser(eictx);
    if (extParser.f != NULL) {
        ZL_SplitParserFn f      = extParser.f;
        ZL_SplitInstructions si = f(&allocState, in);
        ZL_RET_T_IF_NULL(
                ZL_SplitInstructions,
                nodeParameter_invalid,
                si.segmentSizes,
                "external parser failed to provide split instructions");
        return ZL_RESULT_WRAP_VALUE(ZL_SplitInstructions, si);
    }

    // Priority 2 : check for fixed-size parameters
    ZL_RefParam const segmentSizes =
            ZL_Encoder_getLocalParam(eictx, ZL_SPLITN_SEGMENTSIZES_PID);
    ZL_RET_T_IF_EQ(
            ZL_SplitInstructions,
            nodeParameter_invalid,
            segmentSizes.paramId,
            ZL_LP_INVALID_PARAMID,
            "can't find any instruction to split");
    ZL_RET_T_IF_NULL(
            ZL_SplitInstructions,
            nodeParameter_invalid,
            segmentSizes.paramRef,
            "instructions to split are NULL");
    // Handle the case of 0 segments (empty split)
    if (segmentSizes.paramSize == 0) {
        ZL_SplitInstructions r = { NULL, 0 };
        return ZL_RESULT_WRAP_VALUE(ZL_SplitInstructions, r);
    }
    ZL_SplitInstructions r;
    r.segmentSizes = segmentSizes.paramRef;
    r.nbSegments   = segmentSizes.paramSize / sizeof(size_t);
    return ZL_RESULT_WRAP_VALUE(ZL_SplitInstructions, r);
}

// Design note :
// There is no "transform" in this case,
// the Encoder Interface directly maps slices of input
// into output streams, as described in ZL_SPLITN_SEGMENTSIZES_PID parameter.
ZL_Report EI_splitN(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(in);
    ZL_DLOG(BLOCK, "EI_splitN (input:%zu bytes)", ZL_Input_numElts(in));
    ZL_ASSERT_NN(eictx);
    ZL_ASSERT_EQ(
            (int)ZL_Input_type(in)
                    & ~(ZL_Type_serial | ZL_Type_struct | ZL_Type_numeric),
            0);

    ZL_TRY_LET_T(ZL_SplitInstructions, si, getSplitInstructions(eictx, in));

    size_t const inSize   = ZL_Input_numElts(in);
    size_t const eltWidth = ZL_Input_eltWidth(in);
    ZL_DLOG(BLOCK, "EI_splitN: splitting into %zu segments", si.nbSegments);

    // Special case: Empty input & eltWidth > 1 means it must be specified in
    // the header
    if (eltWidth != 1 && si.nbSegments == 0) {
        uint8_t header[ZL_VARINT_LENGTH_64];
        size_t const headerSize = ZL_varintEncode(eltWidth, header);
        ZL_Encoder_sendCodecHeader(eictx, header, headerSize);
    }

    size_t pos = 0;
    ZL_ASSERT_LT(si.nbSegments, INT_MAX);
    for (size_t n = 0; n < si.nbSegments; n++) {
        size_t segSize = si.segmentSizes[n];
        if ((n == si.nbSegments - 1) && (segSize == 0)) {
            // exception: special meaning if last segment size == 0
            ZL_ASSERT_LE(pos, inSize);
            segSize = inSize - pos;
        }
        ZL_DLOG(SEQ, "EI_splitN: segment %zu of size %zu", n, segSize);
        ZL_ERR_IF_GT(
                pos + segSize,
                inSize,
                nodeParameter_invalidValue,
                "split instructions require more length than input");
        ZL_Output* const s = ENC_refTypedStream(
                eictx, 0, eltWidth, segSize, in, pos * eltWidth);
        ZL_ERR_IF_NULL(s, allocation);
        ZL_ERR_IF_ERR(ZL_Output_setIntMetadata(s, ZL_SPLIT_CHANNEL_ID, (int)n));
        pos += segSize;
    }
    ZL_ERR_IF_NE(
            pos,
            inSize,
            nodeParameter_invalidValue,
            "split instructions do not map exactly the entire input");

    return ZL_returnSuccess();
}

static ZL_NodeID getSplitNNodeID(ZL_Type type)
{
    switch (type) {
        case ZL_Type_serial:
            return (ZL_NodeID){ ZL_PrivateStandardNodeID_splitN };
        case ZL_Type_struct:
            return (ZL_NodeID){ ZL_PrivateStandardNodeID_splitN_struct };
        case ZL_Type_numeric:
            return (ZL_NodeID){ ZL_PrivateStandardNodeID_splitN_num };
        case ZL_Type_string:
        default:
            return ZL_NODE_ILLEGAL;
    }
}

ZL_NodeID ZL_Compressor_registerSplitNode_withParams(
        ZL_Compressor* cgraph,
        ZL_Type type,
        const size_t* segmentSizes,
        size_t nbSegments)
{
    if (nbSegments > (size_t)INT_MAX) {
        ZL_LOG(ERROR, "nbSegments is too large (temporary limitation)");
        return ZL_NODE_ILLEGAL;
    }

    ZL_CopyParam const segmentSizesParam = {
        .paramId   = ZL_SPLITN_SEGMENTSIZES_PID,
        .paramPtr  = segmentSizes,
        .paramSize = nbSegments * sizeof(size_t)
    };
    ZL_LocalCopyParams const lgp = { &segmentSizesParam, 1 };

    ZL_LocalParams const lParams = { .copyParams = lgp };
    return ZL_Compressor_cloneNode(cgraph, getSplitNNodeID(type), &lParams);
}

ZL_NodeID ZL_Compressor_registerSplitNode_withParser(
        ZL_Compressor* cgraph,
        ZL_Type type,
        ZL_SplitParserFn f,
        void const* opaque)
{
    ZL_DLOG(SEQ, "ZL_Compressor_registerSplitNode_withParser");
    ZL_RefParam const refParams[2] = {
        { .paramId = ZL_SPLITN_PARSINGF_PID, .paramRef = (void const*)f },
        { .paramId = ZL_SPLITN_PARSING_OPAQUE_PTR_PID, .paramRef = opaque }
    };

    ZL_LocalRefParams const lgp  = { refParams, 2 };
    ZL_LocalParams const lParams = { .refParams = lgp };
    return ZL_Compressor_cloneNode(cgraph, getSplitNNodeID(type), &lParams);
}

void* ZL_SplitState_malloc(ZL_SplitState* state, size_t size)
{
    return ZL_Encoder_getScratchSpace(state->eictx, size);
}

void const* ZL_SplitState_getOpaquePtr(ZL_SplitState* state)
{
    SplitN_ExtParser_s extParser = getExtParser(state->eictx);
    if (extParser.f == NULL) {
        return NULL;
    }
    return extParser.opaque;
}

static ZL_GraphID splitBackendGraph(ZL_Type type)
{
    switch (type) {
        case ZL_Type_serial:
            return ZL_GRAPH_SPLIT_SERIAL;
        case ZL_Type_struct:
            return ZL_GRAPH_SPLIT_STRUCT;
        case ZL_Type_numeric:
            return ZL_GRAPH_SPLIT_NUMERIC;
        case ZL_Type_string:
            return ZL_GRAPH_SPLIT_STRING;
        default:
            return ZL_GRAPH_ILLEGAL;
    }
}

ZL_GraphID ZL_Compressor_registerSplitGraph(
        ZL_Compressor* cgraph,
        ZL_Type type,
        const size_t segmentSizes[],
        const ZL_GraphID successors[],
        size_t nbSegments)
{
    ZL_NodeID sbp_tr = ZL_Compressor_registerSplitNode_withParams(
            cgraph, type, segmentSizes, nbSegments);

    ZL_ParameterizedGraphDesc graphParams = {
        .name           = "zl.split",
        .graph          = splitBackendGraph(type),
        .customGraphs   = successors,
        .nbCustomGraphs = nbSegments,
        .customNodes    = &sbp_tr,
        .nbCustomNodes  = 1,
    };

    return ZL_Compressor_registerParameterizedGraph(cgraph, &graphParams);
}

ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runSplitNode(
        ZL_Edge* input,
        const size_t* segmentSizes,
        size_t nbSegments)
{
    ZL_DLOG(SEQ, "ZL_Edge_runSplitNode");
    ZL_RET_T_IF_GT(
            ZL_EdgeList,
            nodeParameter_invalid,
            nbSegments,
            (size_t)INT_MAX,
            "nbSegments is too large (temporary limitation)");

    ZL_RefParam const segmentSizesParam = {
        .paramId   = ZL_SPLITN_SEGMENTSIZES_PID,
        .paramRef  = segmentSizes,
        .paramSize = nbSegments * sizeof(size_t)
    };
    ZL_LocalRefParams const lrp = { &segmentSizesParam, 1 };

    ZL_LocalParams const lParams = { .refParams = lrp };
    ZL_Type type                 = ZL_Input_type(ZL_Edge_getData(input));
    return ZL_Edge_runNode_withParams(input, getSplitNNodeID(type), &lParams);
}
