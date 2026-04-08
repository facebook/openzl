// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/decompress/decoder_fusion.h"
#include "openzl/codecs/decoder_registry.h"
#include "openzl/common/limits.h"
#include "openzl/common/set.h"
#include "openzl/decompress/dctx2.h"
#include "openzl/decompress/dictx.h"
#include "openzl/zl_input.h"
#include "openzl/zl_output.h"

ZL_DECLARE_SET_TYPE(ZL_RegenSet, ZL_IDType);

static ZL_Report ZL_DecoderFusionDesc_validateImpl(
        const ZL_DecoderFusionDesc* fusion,
        ZL_RegenSet* regenSet)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);

    ZL_ERR_IF_EQ(fusion->pattern.numChildren, 0, logicError);

    const ZL_IDType parentCodec = fusion->pattern.parentCodec;

    for (size_t childIdx = 0; childIdx < fusion->pattern.numChildren;
         ++childIdx) {
        const ZL_DecoderFusionChild child = fusion->pattern.children[childIdx];

        ZL_ERR_IF_EQ(
                child.codec,
                parentCodec,
                temporaryLibraryLimitation,
                "We use CodecID == fusion->pattern.codecID to determine if "
                "it is the parent node during execution. This can be "
                "changed.");
        ZL_ERR_IF_EQ(child.numRegens, 0, logicError);
        for (size_t regen = 0; regen < child.numRegens; ++regen) {
            const ZL_RegenSet_Insert insert =
                    ZL_RegenSet_insertVal(regenSet, child.parentIndices[regen]);
            ZL_ERR_IF(insert.badAlloc, allocation);
            ZL_ERR_IF(
                    !insert.inserted,
                    logicError,
                    "Fusion has duplicate parent indices");
        }
    }

    return ZL_returnSuccess();
}

static ZL_Report ZL_DecoderFusionDesc_validate(
        const ZL_DecoderFusionDesc* fusion)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_RegenSet regenSet = ZL_RegenSet_create(
            (uint32_t)ZL_transformOutStreamsLimit(ZL_MAX_FORMAT_VERSION));
    const ZL_Report result =
            ZL_DecoderFusionDesc_validateImpl(fusion, &regenSet);
    ZL_RegenSet_destroy(&regenSet);
    ZL_ERR_IF_ERR(result);

    return ZL_returnSuccess();
}

static ZL_Report ZL_DecoderFusionState_buildMap(ZL_DecoderFusionState* state)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);

    ZL_DecoderFusionMap_clear(&state->codecFusionMap);
    ZL_DecoderFusionMap_reserve(
            &state->codecFusionMap, (uint32_t)state->numCodecFusions, false);

    ZL_IDType begin = 0;
    for (size_t i = 0; i < state->numCodecFusions; ++i) {
        ZL_ASSERT_SUCCESS(
                ZL_DecoderFusionDesc_validate(&state->codecFusions[i]),
                "Must already be validated");
        if (i == 0) {
            continue;
        }

        ZL_ASSERT_LT(begin, i);
        const ZL_IDType prevCodec =
                state->codecFusions[i - 1].pattern.parentCodec;
        const ZL_IDType currCodec = state->codecFusions[i].pattern.parentCodec;
        ZL_ERR_IF_GT(
                prevCodec,
                currCodec,
                logicError,
                "Fusions must be sorted by parent codec.");
        if (prevCodec != currCodec) {
            const ZL_DecoderFusionMap_Entry entry = {
                prevCodec,
                { begin, (ZL_IDType)i - begin },
            };
            const ZL_DecoderFusionMap_Insert insert =
                    ZL_DecoderFusionMap_insertVal(
                            &state->codecFusionMap, entry);
            ZL_ERR_IF(insert.badAlloc, allocation);
            ZL_ASSERT(insert.inserted);
            begin = (ZL_IDType)i;
        }
    }

    if (begin != state->numCodecFusions) {
        const ZL_DecoderFusionMap_Entry entry = {
            state->codecFusions[state->numCodecFusions - 1].pattern.parentCodec,
            { begin, (ZL_IDType)state->numCodecFusions - begin },
        };
        const ZL_DecoderFusionMap_Insert insert =
                ZL_DecoderFusionMap_insertVal(&state->codecFusionMap, entry);
        ZL_ERR_IF(insert.badAlloc, allocation);
        ZL_ASSERT(insert.inserted);
    }

    return ZL_returnSuccess();
}

ZL_Report ZL_DecoderFusionState_init(ZL_DecoderFusionState* state)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    memset(state, 0, sizeof(*state));

    state->arena = ALLOC_HeapArena_create();
    ZL_ERR_IF_NULL(state->arena, allocation);

    state->codecFusionMap = ZL_DecoderFusionMap_createInArena(
            state->arena, ZL_CUSTOM_TRANSFORM_LIMIT);
    state->codecFusions    = ZL_DecoderFusion_array;
    state->numCodecFusions = ZL_DecoderFusionID_end;

    return ZL_DecoderFusionState_buildMap(state);
}

void ZL_DecoderFusionState_destroy(ZL_DecoderFusionState* state)
{
    if (state->arena) {
        ALLOC_Arena_freeArena(state->arena);
    }
}

void ZL_DecoderFusionState_clearFusions(ZL_DecoderFusionState* state)
{
    ZL_DecoderFusionMap_clear(&state->codecFusionMap);
    state->codecFusions    = NULL;
    state->numCodecFusions = 0;
    ALLOC_Arena_free(state->arena, state->codecFusionStorage);
    state->codecFusionStorage = NULL;
}

ZL_Report ZL_DecoderFusionState_registerFusion(
        ZL_DecoderFusionState* state,
        const ZL_DecoderFusionDesc* fusion)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);

    ZL_ERR_IF_ERR(ZL_DecoderFusionDesc_validate(fusion));

    const bool firstDynamicAlloc =
            state->codecFusionStorage == NULL && state->numCodecFusions > 0;
    ZL_DecoderFusionDesc* codecFusions = ALLOC_Arena_realloc(
            state->arena,
            state->codecFusionStorage,
            (state->numCodecFusions + 1) * sizeof(*state->codecFusionStorage));
    ZL_ERR_IF_NULL(codecFusions, allocation);
    state->codecFusionStorage = codecFusions;
    if (firstDynamicAlloc) {
        memcpy(codecFusions,
               state->codecFusions,
               state->numCodecFusions * sizeof(*codecFusions));
    }

    ZL_DecoderFusionDesc xferedFusion = *fusion;

    ALLOC_ARENA_MALLOC_CHECKED(
            ZL_DecoderFusionChild,
            xferedChildren,
            fusion->pattern.numChildren,
            state->arena);
    xferedFusion.pattern.children = xferedChildren;
    for (size_t c = 0; c < fusion->pattern.numChildren; ++c) {
        xferedChildren[c] = fusion->pattern.children[c];
        ALLOC_ARENA_MALLOC_CHECKED(
                uint32_t,
                xferedParentIndices,
                xferedChildren[c].numRegens,
                state->arena);
        ZL_ASSERT_GT(xferedChildren[c].numRegens, 0);
        memcpy(xferedParentIndices,
               xferedChildren[c].parentIndices,
               xferedChildren[c].numRegens * sizeof(*xferedParentIndices));
        xferedChildren[c].parentIndices = xferedParentIndices;
    }

    // Insert to the end of the range for codecID
    const ZL_IDType codecID = fusion->pattern.parentCodec;
    size_t idx              = state->numCodecFusions;
    while (idx > 0 && codecID < codecFusions[idx - 1].pattern.parentCodec) {
        codecFusions[idx] = codecFusions[idx - 1];
        --idx;
    }
    codecFusions[idx] = xferedFusion;

    state->codecFusions = codecFusions;
    ++state->numCodecFusions;

    // Rebuild the map
    return ZL_DecoderFusionState_buildMap(state);
}

void* ZL_DecoderFusion_getScratchSpace(ZL_DecoderFusion* state, size_t size)
{
    return ALLOC_Arena_malloc(state->workspaceArena, size);
}

ZL_Output* ZL_DecoderFusion_createTypedStream(
        ZL_DecoderFusion* state,
        int index,
        size_t eltsCapacity,
        size_t eltWidth)
{
    size_t dstCapacity;
    if (ZL_overflowMulST(eltsCapacity, eltWidth, &dstCapacity)) {
        return NULL;
    }
    if (eltWidth == 0) {
        return NULL;
    }
    if (index < 0 || index >= (int)state->numRegenStreams) {
        return NULL;
    }
    const DTransform* parentCodec = state->codecs[state->numCodecs - 1];
    return ZL_codemodDataAsOutput(DCTX_newStream(
            state->dctx,
            state->regenStreamIDs[index],
            DT_getRegenType(parentCodec, index),
            eltWidth,
            eltsCapacity));
}

ZL_Output* ZL_DecoderFusion_createStringStream(
        ZL_DecoderFusion* state,
        int index,
        size_t nbStringsMax,
        size_t sumStringLensMax)
{
    ZL_Output* const stringS = ZL_DecoderFusion_createTypedStream(
            state, index, sumStringLensMax, 1);
    if (stringS == NULL) {
        return NULL;
    }
    if (ZL_Output_type(stringS) != ZL_Type_string) {
        return NULL;
    }
    uint32_t* const strLens =
            ZL_Output_reserveStringLens(stringS, nbStringsMax);
    if (strLens == NULL) {
        return NULL;
    }
    return stringS;
}

ZL_RESULT_OF(ZL_RBuffer)
ZL_DecoderFusion_getCodecHeader(const ZL_DecoderFusion* state, size_t idx)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_RBuffer, state->dctx);
    ZL_ERR_IF_GE(idx, state->numCodecs, logicError);
    return ZL_WRAP_VALUE(state->codecHeaders[idx]);
}

ZL_RESULT_OF(ZL_CodecInputs)
ZL_DecoderFusion_getCodecInputs(const ZL_DecoderFusion* state, size_t idx)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_CodecInputs, state->dctx);
    ZL_ERR_IF_GE(idx, state->numCodecs, logicError);

    const DFH_NodeInfo* nodeInfo = state->nodeInfos[idx];
    const size_t total           = nodeInfo->numInputStreams;
    const size_t nbSOs           = total - nodeInfo->nbVOs;
    ZL_ASSERT_LE(nbSOs, total);

    const ZL_Input* const* inputs =
            ZL_codemodDatasAsInputs(state->codecInputs[idx]);

    ZL_CodecInputs result = {
        .singleton = { .inputs = inputs, .numInputs = nbSOs },
        .variable  = { .inputs = inputs + nbSOs, .numInputs = total - nbSOs },
    };
    return ZL_WRAP_VALUE(result);
}

ZL_RESULT_OF(ZL_InputArray)
ZL_DecoderFusion_runCodec(ZL_DecoderFusion* state, size_t idx)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_InputArray, state->dctx);
    ZL_ERR_IF_GE(idx, state->numCodecs, logicError);
    const DFH_NodeInfo* nodeInfo = state->nodeInfos[idx];
    ZL_ERR_IF_ERR(DCTX_runDecoder(
            state->dctx, nodeInfo, /* withinFusedDecoder */ true));

    const ZL_IDType inputEndIdx =
            nodeInfo->inputStreamBaseIdx + nodeInfo->numInputStreams;
    // Collect outputs
    ALLOC_ARENA_MALLOC_CHECKED(
            const ZL_Input*,
            outputs,
            nodeInfo->nbRegens,
            state->workspaceArena);
    for (size_t n = 0; n < nodeInfo->nbRegens; n++) {
        const ZL_IDType regenIdx = inputEndIdx + nodeInfo->regenDistances[n];
        const ZL_Data* data = ZL_DCtx_getConstStream(state->dctx, regenIdx);
        outputs[n]          = ZL_codemodDataAsInput(data);
    }

    ZL_InputArray result = {
        .inputs    = outputs,
        .numInputs = nodeInfo->nbRegens,
    };
    return ZL_WRAP_VALUE(result);
}

bool ZL_DecoderFusionDesc_fuseIfMatches(
        const ZL_DecoderFusionDesc* fusion,
        DFH_NodeInfo* nodeInfo,
        const ZL_DCtx_DataInfo* dataInfo,
        size_t parentNodeIndex)
{
    DFH_NodeInfo* const parentNodeInfo = &nodeInfo[parentNodeIndex];
    ZL_ASSERT_NULL(parentNodeInfo->fusion);

    if (parentNodeInfo->trpid.trt != trt_standard
        || parentNodeInfo->trpid.trid != fusion->pattern.parentCodec) {
        return false; // Parent codec ID doesn't match
    }

    const size_t parentDataInfoBegin = parentNodeInfo->inputStreamBaseIdx;
    const size_t parentNumIns        = parentNodeInfo->numInputStreams;

    for (size_t childIdx = 0; childIdx < fusion->pattern.numChildren;
         ++childIdx) {
        ZL_DecoderFusionChild child = fusion->pattern.children[childIdx];

        if (child.parentIndices[0] >= parentNumIns) {
            return false; // Parent does not have enough inputs
        }

        const ZL_IDType childNodeIdx = ZL_DecoderFusionDesc_getProducerNodeIdx(
                fusion, childIdx, parentNodeInfo, dataInfo);
        if (childNodeIdx == ZL_PRODUCER_STORE) {
            return false; // Parent input is stored in frame
        }
        const DFH_NodeInfo* childNodeInfo = &nodeInfo[childNodeIdx];

        if (childNodeInfo->trpid.trt != trt_standard
            || childNodeInfo->trpid.trid != child.codec) {
            return false; // Child codec ID doesn't match
        }

        if (childNodeInfo->fusion != NULL) {
            // This codec is already involved in another fusion.
            return false;
        }

        if (childNodeInfo->nbRegens != child.numRegens) {
            return false; // Child produces wrong number of outputs
        }

        const size_t childInputEndIdx = childNodeInfo->inputStreamBaseIdx
                + childNodeInfo->numInputStreams;
        // Validate the all other regenerated streams map to the correct parent
        // input.
        // NOTE: We can start at index 1 because getProducerNodeIdx() uses index
        // 0 to find the child node, so we know the mapping is correct.
        for (size_t regenIdx = 1; regenIdx < child.numRegens; ++regenIdx) {
            if (child.parentIndices[regenIdx] >= parentNumIns) {
                return false; // Parent does not have enough inputs
            }
            // NOTE: Inputs are stored in reverse order
            const size_t parentStreamID = parentDataInfoBegin + parentNumIns - 1
                    - child.parentIndices[regenIdx];
            if (childInputEndIdx + childNodeInfo->regenDistances[regenIdx]
                != parentStreamID) {
                // Child's `regenIdx`th output is not mapped to
                // `parentIndices[regenIdx]`.
                return false;
            }
        }
    }

    parentNodeInfo->fusion = fusion;
    for (size_t childIdx = 0; childIdx < fusion->pattern.numChildren;
         ++childIdx) {
        const ZL_IDType childNodeIdx = ZL_DecoderFusionDesc_getProducerNodeIdx(
                fusion, childIdx, parentNodeInfo, dataInfo);
        ZL_ASSERT_NE(childNodeIdx, ZL_PRODUCER_STORE);
        nodeInfo[childNodeIdx].fusion = fusion;
    }

    return true;
}

ZL_IDType ZL_DecoderFusionDesc_getProducerNodeIdx(
        const ZL_DecoderFusionDesc* fusion,
        size_t childIdx,
        const DFH_NodeInfo* parentNodeInfo,
        const ZL_DCtx_DataInfo* dataInfo)
{
    ZL_DecoderFusionChild child = fusion->pattern.children[childIdx];
    ZL_ASSERT_NE(child.numRegens, 0);
    const size_t parentInIdx = child.parentIndices[0];
    // NOTE: Inputs are stored in reverse order
    const size_t parentDataInfoIdx = parentNodeInfo->inputStreamBaseIdx
            + parentNodeInfo->numInputStreams - 1 - parentInIdx;
    const ZL_DCtx_DataInfo* regenInfo = &dataInfo[parentDataInfoIdx];
    ZL_ASSERT_NULL(regenInfo->appendOpt);

    return regenInfo->producerNodeIdx;
}
