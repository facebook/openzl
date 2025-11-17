// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/segmenter.h"
#include "openzl/common/allocation.h"
#include "openzl/common/assertion.h"
#include "openzl/common/stream.h" // STREAM_*
#include "openzl/common/vector.h"
#include "openzl/compress/cctx.h"        // CCTX_*
#include "openzl/compress/localparams.h" // LP_*
#include "openzl/compress/rtgraphs.h"
#include "openzl/zl_data.h"   // ZL_Data, ZL_Type
#include "openzl/zl_errors.h" // ZL_RESULT_DECLARE_SCOPE_REPORT
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_segmenter.h"

/* ===   state management   === */

struct ZL_Segmenter_s {
    const ZL_SegmenterDesc* segDesc;
    ZL_CCtx* cctx; // for global parameters, error context
    RTGraph* rtgm;
    ZL_Data** inputs;
    size_t nbInputs;
    size_t* consumed;
    Arena* arena;
    Arena* chunkArena;
};

/**
 * Implementation Notes for SEGM_init():
 *
 * Memory Strategy: Uses arena allocation for all segmenter state to ensure
 * automatic cleanup when the arena is deallocated. This eliminates the need
 * for explicit cleanup functions and prevents memory leaks.
 *
 * Input Stream Setup: Erapper streams that reference input buffers already
 * referenced within RTGraph. This allows efficient slicing during chunk
 * processing while maintaining the original stream integrity.
 *
 * Consumption Tracking: Initializes consumed[] array to zero using calloc
 * to track how much of each input has been processed. This enables proper
 * resumption of processing from the correct position in subsequent chunks.
 *
 * Alternative Considered: Direct stream manipulation is considered.
 * It will require a refactor of the STREAM_* API.
 */
ZL_Segmenter* SEGM_init(
        const ZL_SegmenterDesc* segDesc,
        size_t nbInputs,
        ZL_CCtx* cctx,
        RTGraph* rtgm,
        Arena* arena,
        Arena* chunkArena)
{
    ZL_DLOG(BLOCK, "SEGM_init");
    ZL_Segmenter* seg = ALLOC_Arena_malloc(arena, sizeof(ZL_Segmenter));
    if (seg == NULL)
        return NULL;
    seg->segDesc    = segDesc;
    seg->cctx       = cctx;
    seg->rtgm       = rtgm;
    seg->arena      = arena;
    seg->chunkArena = chunkArena;
    ZL_ASSERT_EQ(nbInputs, VECTOR_SIZE(rtgm->streams));
    seg->nbInputs = nbInputs;
    seg->inputs   = ALLOC_Arena_malloc(arena, nbInputs * sizeof(ZL_Data*));
    if (seg->inputs == NULL)
        return NULL;
    seg->consumed = ALLOC_Arena_calloc(arena, nbInputs * sizeof(size_t));
    if (seg->consumed == NULL)
        return NULL;
    for (size_t n = 0; n < nbInputs; n++) {
        seg->inputs[n] =
                STREAM_createInArena(arena, (ZL_DataID){ (ZL_IDType)n });
        ZL_Report ref = STREAM_refStreamWithoutRefCount(
                seg->inputs[n], VECTOR_AT(rtgm->streams, n).stream);
        if (ZL_isError(ref))
            return NULL;
        ZL_DLOG(BLOCK,
                "input %zu: size = %zu, type = %u",
                n,
                ZL_Data_contentSize(seg->inputs[n]),
                ZL_Data_type(seg->inputs[n]));
    }
    RTGM_reset(rtgm); // Clean the RTGraph to start the 1st chunk
    return seg;
}

/* ===   internal actions   === */

/**
 * SEGM_runSegmenter(): acts as a thin wrapper around the user-provided
 * segmenter callback. User code is in charge of actual chunking logic. The
 * wrapper just checks that all conditions are correctly respected. In current
 * implementation, it enforces that input is entirely consumed.
 */
ZL_Report SEGM_runSegmenter(ZL_Segmenter* segCtx)
{
    ZL_ASSERT_NN(segCtx);
    ZL_SegmenterFn const segfn = segCtx->segDesc->segmenterFn;
    ZL_ASSERT_NN(segfn);
    ZL_Report const r = segfn(segCtx);

    // if successful, check that all inputs were consumed
    if (!ZL_isError(r)) {
        for (size_t n = 0; n < segCtx->nbInputs; n++) {
            ZL_RET_R_IF_LT(
                    segmenter_inputNotConsumed,
                    segCtx->consumed[n],
                    ZL_Data_numElts(segCtx->inputs[n]),
                    "input %zu wasn't entirely consumed",
                    n);
        }
    }
    return r;
}

/* ===   accessors   === */

/* Special state pointer */
const void* ZL_Segmenter_getOpaquePtr(const ZL_Segmenter* segCtx)
{
    return segCtx->segDesc->opaque.ptr;
}

/* Consultation request for Global parameters */
int ZL_Segmenter_getCParam(const ZL_Segmenter* segCtx, ZL_CParam gparam)
{
    ZL_ASSERT_NN(segCtx);
    return CCTX_getAppliedGParam(segCtx->cctx, gparam);
}

/* Consultation requests for Local parameters */
ZL_IntParam ZL_Segmenter_getLocalIntParam(
        const ZL_Segmenter* segCtx,
        int intParamId)
{
    ZL_ASSERT_NN(segCtx);
    return LP_getLocalIntParam(&segCtx->segDesc->localParams, intParamId);
}

ZL_RefParam ZL_Segmenter_getLocalRefParam(
        const ZL_Segmenter* segCtx,
        int refParamId)
{
    ZL_ASSERT_NN(segCtx);
    return LP_getLocalRefParam(&segCtx->segDesc->localParams, refParamId);
}

const ZL_LocalParams* ZL_Segmenter_getLocalParams(const ZL_Segmenter* segCtx)
{
    ZL_ASSERT_NN(segCtx);
    return &segCtx->segDesc->localParams;
}

/* Consultation request for Custom Successor Graphs */
ZL_GraphIDList ZL_Segmenter_getCustomGraphs(const ZL_Segmenter* segCtx)
{
    ZL_ASSERT_NN(segCtx);
    return (ZL_GraphIDList){
        .graphids   = segCtx->segDesc->customGraphs,
        .nbGraphIDs = segCtx->segDesc->numCustomGraphs,
    };
}

/* Number of Inputs received by the Segmenter */
size_t ZL_Segmenter_numInputs(const ZL_Segmenter* segCtx)
{
    ZL_ASSERT_NN(segCtx);
    return segCtx->nbInputs;
}

/**
 * Implementation Notes for ZL_Segmenter_getInput():
 *
 * Stream Creation Strategy: Creates a new temporary stream rather than
 * returning a direct reference to the session input. This is because Inputs are
 * currently immutable.
 *
 * Consumption Offset: Uses STREAM_refStreamFrom() to automatically start the
 * returned stream from the already-consumed position, giving segmenters a view
 * of only the remaining unprocessed data.
 *
 * Arena Allocation: Allocates the temporary stream in the main arena rather
 * than chunk arena since the stream may be accessed multiple times during
 * segmentation planning.
 */
const ZL_Input* ZL_Segmenter_getInput(
        const ZL_Segmenter* segCtx,
        size_t inputID)
{
    ZL_ASSERT_NN(segCtx);
    if (inputID >= segCtx->nbInputs)
        return NULL;
    ZL_Data* const sessionInput = segCtx->inputs[inputID];
    ZL_ASSERT_NN(sessionInput);
    size_t alreadyConsumed = segCtx->consumed[inputID];
    if (alreadyConsumed > ZL_Data_numElts(sessionInput))
        return NULL;
    ZL_Data* const chunkInput = STREAM_createInArena(
            segCtx->arena, (ZL_DataID){ (ZL_IDType)inputID });
    if (chunkInput == NULL)
        return NULL;
    ZL_Report r = STREAM_refEndStreamWithoutRefCount(
            chunkInput, sessionInput, alreadyConsumed);
    if (ZL_isError(r))
        return NULL;
    return ZL_codemodDataAsInput(chunkInput);
}

/**
 * Bulk request to get the number of Elements of all Inputs at once.
 * @param numElts must be already allocated, and sized appropriately.
 * @param nbInputs can be determined with ZL_Segmenter_nbInputs().
 * @return success, or an error if there is problem (wrong nbInputs for example)
 * @note numElts can vary over time, as Inputs get progressively consumed by
 * ZL_Segmenter_processChunk().
 */
ZL_Report ZL_Segmenter_getNumElts(
        const ZL_Segmenter* segCtx,
        size_t numElts[],
        size_t nbInputs)
{
    for (size_t n = 0; n < nbInputs; n++) {
        ZL_ASSERT_LE(segCtx->consumed[n], ZL_Data_numElts(segCtx->inputs[n]));
        numElts[n] = ZL_Data_numElts(segCtx->inputs[n]) - segCtx->consumed[n];
    }
    return ZL_returnSuccess();
}

/* ===   public actions   === */

/* Memory allocation:
 * */
void* ZL_Segmenter_getScratchSpace(ZL_Segmenter* segCtx, size_t size)
{
    return ALLOC_Arena_malloc(segCtx->arena, size);
}

/**
 * Implementation Notes for ZL_Segmenter_processChunk():
 *
 * Memory Allocation Strategy: Uses chunk arena for temporary allocations
 * (chunkInputs, rtsids) that are freed after processing. Session-lifetime
 * objects like stream references use the main arena.
 *
 * Stream Slicing Approach: Creates stream slices via STREAM_refStreamSlice()
 * rather than copying data. This provides zero-copy chunk processing, though it
 * adds some maintenance burden on proper reference counting at cleanup time.
 *
 * Consumption Tracking: consumed[] array is updated before graph execution.
 *
 * RTGraph Reset Strategy: Calls RTGM_reset() before each chunk to ensure
 * clean state, then registers chunk inputs as new runtime streams.
 *
 * Protection Level: Uses depth=1 for graph execution, providing the highest
 * protection level that still allows graphs to make redirection decisions.
 *
 * Cleanup Pattern: Manual cleanup with proper STREAM_free() calls to handle
 * reference counting, followed by CCTX_cleanChunk() for context cleanup.
 */
ZL_Report ZL_Segmenter_processChunk(
        ZL_Segmenter* segCtx,
        const size_t numElts[],
        size_t numInputs,
        ZL_GraphID startingGraphID,
        const ZL_RuntimeGraphParameters* rGraphParams)
{
    WAYPOINT(
            on_ZL_Segmenter_processChunk_start,
            segCtx,
            numElts,
            numInputs,
            startingGraphID,
            rGraphParams);

    ZL_ASSERT_NN(segCtx);
    ZL_CCtx* const cctx = segCtx->cctx;
    ZL_ASSERT_NN(cctx);
    ZL_RESULT_DECLARE_SCOPE_REPORT(cctx);
    ZL_DLOG(SEQ, "ZL_Segmenter_processChunk (%zu Inputs)", numInputs);

    ZL_ERR_IF_NE(
            numInputs, ZL_Segmenter_numInputs(segCtx), graph_invalidNumInputs);

    // Define Graph's inputs as a slice of Session's inputs
    ALLOC_ARENA_MALLOC_CHECKED(
            ZL_Data*, chunkInputs, numInputs, segCtx->chunkArena);
    for (size_t n = 0; n < numInputs; n++) {
        ZL_ERR_IF_GT(
                numElts[n],
                ZL_Data_numElts(segCtx->inputs[n]),
                parameter_invalid);
        chunkInputs[n] = STREAM_createInArena(
                segCtx->arena, (ZL_DataID){ (ZL_IDType)n });
        ZL_ERR_IF_NULL(chunkInputs[n], allocation);
        ZL_ERR_IF_ERR(STREAM_refStreamSliceWithoutRefCount(
                chunkInputs[n],
                segCtx->inputs[n],
                segCtx->consumed[n],
                numElts[n]));
        segCtx->consumed[n] += numElts[n];
    }

    ALLOC_ARENA_MALLOC_CHECKED(
            RTStreamID, rtsids, numInputs, segCtx->chunkArena);
    RTGM_reset(segCtx->rtgm);
    for (size_t n = 0; n < numInputs; n++) {
        ZL_TRY_LET(
                RTStreamID, rtsid, RTGM_refInput(segCtx->rtgm, chunkInputs[n]));
        rtsids[n] = rtsid;
    }

    // Run the starting Graph on the Inputs
    // This is depth 1, which is the highest level of protection,
    // allowing the Graph to make redirection decisions if need be.
    // Note: depth==0 means "unprotected"
    ZL_ERR_IF_ERR(CCTX_runSuccessor(
            cctx,
            startingGraphID,
            rGraphParams,
            rtsids,
            numInputs,
            /* depth */ 1));

    ZL_Report r = CCTX_flushChunk(cctx, (void*)chunkInputs, numInputs);

    // clean and exit
    for (size_t n = 0; n < numInputs; n++) {
        // We want to free properly, in case they "lock" their reference via
        // refCount
        STREAM_free(chunkInputs[n]);
    }
    CCTX_cleanChunk(cctx);

    WAYPOINT(on_ZL_Segmenter_processChunk_end, segCtx, r);
    return r;
}

ZL_CONST_FN
ZL_OperationContext* ZL_Segmenter_getOperationContext(ZL_Segmenter* sctx)
{
    if (sctx == NULL) {
        return NULL;
    }
    return ZL_CCtx_getOperationContext(sctx->cctx);
}
