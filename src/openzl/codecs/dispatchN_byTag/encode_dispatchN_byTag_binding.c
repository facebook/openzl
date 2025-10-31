// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/dispatchN_byTag/encode_dispatchN_byTag_binding.h"
#include "openzl/codecs/dispatchN_byTag/encode_dispatchN_byTag_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/common/logging.h"            // ZL_DLOG
#include "openzl/shared/mem.h"                // ZL_memset
#include "openzl/shared/numeric_operations.h" // NUMOP_*
#include "openzl/zl_graph_api.h"

/* condition : all inputs are valid
 */
static void sizes_byTags(
        size_t outSizes[],
        const size_t sizes[],
        const unsigned tags[],
        size_t nbSizes,
        size_t nbTags)
{
    ZL_ASSERT_NN(outSizes);
    ZL_memset(outSizes, 0, nbTags * sizeof(size_t));
    if (nbSizes) {
        ZL_ASSERT_NN(sizes);
        ZL_ASSERT_NN(tags);
        ZL_ASSERT_GT(nbTags, 0);
    }
    for (size_t n = 0; n < nbSizes; n++) {
        unsigned const t = tags[n];
        ZL_ASSERT_LT(t, nbTags);
        outSizes[t] += sizes[n];
    }
}

/* ----- DispatchN byTag --------- */

struct ZL_DispatchState_s {
    ZL_Encoder* eictx;
    char const* message;
};

typedef struct {
    ZL_DispatchParserFn f;
    void const* opaque;
} DispatchNBT_ExtParser_s;

ZL_RESULT_DECLARE_TYPE(ZL_DispatchInstructions);

static DispatchNBT_ExtParser_s const* getExtParser(ZL_Encoder const* eictx)
{
    ZL_CopyParam const gpParsef =
            ZL_Encoder_getLocalCopyParam(eictx, ZL_DISPATCH_PARSINGFN_PID);

    if (gpParsef.paramId != ZL_DISPATCH_PARSINGFN_PID) {
        return NULL;
    }

    return (const DispatchNBT_ExtParser_s*)gpParsef.paramPtr;
}

static ZL_RESULT_OF(ZL_DispatchInstructions)
        getSplitInstructions(ZL_Encoder* eictx, const ZL_Input* in)
{
    ZL_DLOG(SEQ, "getSplitInstructions()");

    if (ZL_Input_numElts(in) == 0) {
        // Special case for empty input: No splits
        ZL_DispatchInstructions si = { NULL, NULL, 0, 0 };
        return ZL_RESULT_WRAP_VALUE(ZL_DispatchInstructions, si);
    }

    ZL_RefParam const param =
            ZL_Encoder_getLocalParam(eictx, ZL_DISPATCH_INSTRUCTIONS_PID);
    if (param.paramRef != NULL) {
        ZL_DispatchInstructions const* instructions =
                (ZL_DispatchInstructions const*)param.paramRef;
        return ZL_RESULT_WRAP_VALUE(ZL_DispatchInstructions, *instructions);
    }

    ZL_DispatchState state = { eictx, NULL };

    DispatchNBT_ExtParser_s const* s = getExtParser(eictx);
    ZL_RET_T_IF_NULL(
            ZL_DispatchInstructions,
            nodeParameter_invalid,
            s,
            "dispatchN parser not provided");
    ZL_DispatchParserFn f      = s->f;
    ZL_DispatchInstructions si = f(&state, in);
    if (si.segmentSizes == NULL) {
        if (state.message != NULL) {
            ZL_RET_T_ERR(
                    ZL_DispatchInstructions,
                    nodeParameter_invalid,
                    "External dispatchN parser failed with message: %s",
                    state.message);
        } else {
            ZL_RET_T_ERR(
                    ZL_DispatchInstructions,
                    nodeParameter_invalid,
                    "external dispatchN parser failed to provide split instructions");
        }
    }
    return ZL_RESULT_WRAP_VALUE(ZL_DispatchInstructions, si);
}

ZL_Report
EI_dispatchN_byTag(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_NN(in);
    size_t const inputSize = ZL_Input_numElts(in);
    ZL_DLOG(BLOCK, "EI_dispatchN_byTag (in:%zu bytes)", inputSize);
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);
    ZL_ASSERT_NN(eictx);

    ZL_TRY_LET_CONST_T(
            ZL_DispatchInstructions, si, getSplitInstructions(eictx, in));

    ZL_DLOG(BLOCK,
            "EI_dispatchN_byTag: splitting %zu segments into %zu streams",
            si.nbSegments,
            si.nbTags);

    // Here : check parser's output
    size_t const maxSegmentSize =
            NUMOP_findMaxST(si.segmentSizes, si.nbSegments);
    // Must check too see if any are too large to guard against overflow
    if (ZL_Encoder_getCParam(eictx, ZL_CParam_formatVersion) < 20) {
        ZL_RET_R_IF_GE(temporaryLibraryLimitation, si.nbTags, 256);
    } else {
        ZL_RET_R_IF_GE(temporaryLibraryLimitation, si.nbTags, 1 << 16);
    }
    ZL_RET_R_IF_GT(
            nodeParameter_invalidValue,
            maxSegmentSize,
            inputSize,
            "EI_dispatchN_byTag: One of the segment sizes is bigger than the input size");
    size_t const parserTotalSize =
            NUMOP_sumArrayST(si.segmentSizes, si.nbSegments);
    ZL_RET_R_IF_NE(
            nodeParameter_invalidValue,
            parserTotalSize,
            inputSize,
            "EI_dispatchN_byTag: the external parser provides invalid total size");

    ZL_RET_R_IF(
            nodeParameter_invalidValue,
            !NUMOP_underLimit(si.tags, si.nbSegments, si.nbTags),
            "EI_dispatchN_byTag: external parser returns invalid tags!");

    // Dimension and allocate output streams
    size_t const tagsWidth     = si.nbTags == 0
                ? 1
                : NUMOP_numericWidthForValue((size_t)si.nbTags - 1);
    size_t const segSizesWidth = NUMOP_numericWidthForValue(maxSegmentSize);

    ZL_Output* const outTags = ZL_Encoder_createTypedStream(
            eictx, dnbt_tags, si.nbSegments, tagsWidth);
    ZL_RET_R_IF_NULL(allocation, outTags);
    NUMOP_writeNumerics_fromU32(
            ZL_Output_ptr(outTags), tagsWidth, si.tags, si.nbSegments);
    ZL_RET_R_IF_ERR(ZL_Output_commit(outTags, si.nbSegments));

    ZL_Output* const segSizes = ZL_Encoder_createTypedStream(
            eictx, dnbt_segSizes, si.nbSegments, segSizesWidth);
    ZL_RET_R_IF_NULL(allocation, segSizes);
    NUMOP_writeNumerics_fromST(
            ZL_Output_ptr(segSizes),
            segSizesWidth,
            si.segmentSizes,
            si.nbSegments);
    ZL_RET_R_IF_ERR(ZL_Output_commit(segSizes, si.nbSegments));

    ZL_STATIC_ASSERT(
            sizeof(size_t) == sizeof(void*),
            "not necessarily true, will revisit if a platform that doesn't respect this assumption is needed");
    size_t const workSize = 2 * si.nbTags * sizeof(size_t);
    void* const workspace = ZL_Encoder_getScratchSpace(eictx, workSize);
    ZL_RET_R_IF_NULL(allocation, workspace);
    ZL_ASSERT((size_t)workspace % sizeof(size_t) == 0);
    size_t* const outSizes  = workspace;
    void** const outBuffers = ((void**)workspace) + si.nbTags;

    sizes_byTags(outSizes, si.segmentSizes, si.tags, si.nbSegments, si.nbTags);

    for (size_t n = 0; n < si.nbTags; n++) {
        ZL_Output* const out = ZL_Encoder_createTypedStream(
                eictx, dnbt_segments, outSizes[n], 1);
        ZL_RET_R_IF_NULL(allocation, out);
        outBuffers[n] = ZL_Output_ptr(out);
        ZL_RET_R_IF_ERR(ZL_Output_commit(out, outSizes[n]));
        ZL_RET_R_IF_ERR(
                ZL_Output_setIntMetadata(out, ZL_DISPATCH_CHANNEL_ID, (int)n));
    }

    ZL_dispatchN_byTag(
            outBuffers,
            si.segmentSizes,
            si.tags,
            si.nbSegments,
            ZL_Input_ptr(in),
            inputSize);

    return ZL_returnSuccess();
}

ZL_NodeID ZL_Compressor_registerDispatchNode(
        ZL_Compressor* cgraph,
        ZL_DispatchParserFn f,
        void const* opaque)
{
    ZL_DLOG(SEQ, "ZL_Compressor_registerDispatchNode");
    DispatchNBT_ExtParser_s const s = { f, opaque };
    ZL_CopyParam const ssp          = { .paramId   = ZL_DISPATCH_PARSINGFN_PID,
                                        .paramPtr  = &s,
                                        .paramSize = sizeof(s) };

    ZL_LocalCopyParams const lgp = { &ssp, 1 };
    ZL_LocalParams const lParams = { .copyParams = lgp };
    return ZL_Compressor_cloneNode(cgraph, ZL_NODE_DISPATCH, &lParams);
}

void* ZL_DispatchState_malloc(ZL_DispatchState* state, size_t size)
{
    return ZL_Encoder_getScratchSpace(state->eictx, size);
}

void const* ZL_DispatchState_getOpaquePtr(ZL_DispatchState const* state)
{
    DispatchNBT_ExtParser_s const* s = getExtParser(state->eictx);
    if (s == NULL) {
        return NULL;
    }
    return s->opaque;
}

ZL_DispatchInstructions ZL_DispatchState_returnError(
        ZL_DispatchState* state,
        char const* message)
{
    size_t const len = strlen(message);
    char* msg        = (char*)ZL_DispatchState_malloc(state, len + 1);
    if (msg != NULL) {
        memcpy(msg, message, len + 1);
        assert(msg[len] == '\0');
        state->message = msg;
    }
    return (ZL_DispatchInstructions){ NULL, NULL, 0, 0 };
}

ZL_RESULT_OF(ZL_EdgeList)
ZL_Edge_runDispatchNode(
        ZL_Edge* sctx,
        const ZL_DispatchInstructions* instructions)
{
    ZL_DLOG(SEQ, "ZL_Edge_runDispatchNode");
    ZL_LocalParams const lParams =
            ZL_LP_1REFPARAM(ZL_DISPATCH_INSTRUCTIONS_PID, instructions);
    return ZL_Edge_runNode_withParams(sctx, ZL_NODE_DISPATCH, &lParams);
}
