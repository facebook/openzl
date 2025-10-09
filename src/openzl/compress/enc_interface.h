// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_COMPRESS_CBINDING_H
#define ZSTRONG_COMPRESS_CBINDING_H

#include "openzl/common/allocation.h"
#include "openzl/compress/cnode.h"          // CNode
#include "openzl/compress/compress_types.h" // InternalTypedTransform_Desc
#include "openzl/compress/rtgraphs.h"       // RTNodeID
#include "openzl/compress/trStates.h"       // TrStates
#include "openzl/shared/portability.h"
#include "openzl/zl_localParams.h"

ZL_BEGIN_C_DECLS

#if 0
// Public symbols already declared in public headers :
// Note : Public symbols use the prefix ZL_Encoder_*

ZL_Data* ZL_Encoder_createTypedStream(
        ZL_Encoder* ectx,
        int outStreamIndex,
        size_t eltsCapacity,
        size_t eltWidth);
void ZL_Encoder_sendCodecHeader(ZL_Encoder* ei, const void* trh, size_t trhSize);

int ZL_Encoder_getCParam(const ZL_Encoder* eic, ZL_CParam gparam);
ZL_LocalCopyParams ZL_Encoder_getLocalCopyParams(const ZL_Encoder* eic);

#endif

/* API Note:
 * Private symbols use the prefix ENC_*
 */

// ZL_Encoder : Compression Binding Context
// This object is opaque at public level,
// though the structure definition is accessible within the project.
// It tracks information required to contextualize the current binding
// such as its position within the RT_graph, and its output streams.
// Used by : cgraph.c, CGraph_splitAdaptor()

struct ZL_Encoder_s {
    /// link to parent cctx
    ZL_CCtx* cctx;
    /// specific for internal transforms
    const void* privateParam;
    const void* opaquePtr;
    /// Needed to request local parameters within RT_graph,
    /// built during graph traversal.
    /// Inside RTNodes, there is a reference to model Graph
    /// within immutable CGraph, which contains transform's
    /// definition (including its output streams).
    RTNodeID rtnodeid;
    /// Complete node definition, including parameters
    /// For transforms getState
    const CNode* cnode;
    /// Only parameters, for Selectors that don't provide cnode.
    const ZL_LocalParams* lparams;
    /// Checks that Transform Header is only sent once
    int hasSentTrHeader;
    /// Allocator to use for temporary allocations that
    /// are scoped to the current context
    Arena* wkspArena;
    /// The error returned by sendTransformHeader, if any
    ZL_Report sendTransformHeaderError;
    /// Store cached states
    CachedStates* cachedStates;
}; /// typedef'd to ZL_Encoder within zs2_transform_api.h

/* ENC_initEICtx():
 * Initializes a new EICtx
 */
ZL_Report ENC_initEICtx(
        ZL_Encoder* eictx,
        ZL_CCtx* cctx,
        Arena* wkspArena,
        const RTNodeID* rtnodeid,
        const CNode* cnode,
        const ZL_LocalParams* lparams,
        CachedStates* cachedStates);

/* ENC_destroyEICtx:
 * Cleans up an EICtx after use,
 * clears all memory allocated during its lifetime.
 * @eictx shouldn't be used after destroyed.
 */
void ENC_destroyEICtx(ZL_Encoder* eictx);

/* ENC_runTransform() :
 * invoke transform, controls conditions and outcome
 * @return : error, or nb of output streams created
 */
ZL_Report ENC_runTransform(
        const InternalTransform_Desc* trDesc,
        const ZL_Data* inputs[],
        size_t nbInputs,
        ZL_NodeID nodeid,
        RTNodeID rtnodeid,
        const CNode* cnode,
        const ZL_LocalParams* lparams, // optional, can be NULL
        ZL_CCtx* parentCCtx,
        Arena* wkspArena,
        CachedStates* trstates);

/* ENC_refTypedStream() :
 * create a new stream, of type defined by @outcomeIndex,
 * which is just a reference to a slice into another stream @ref that will
 * outlive the new stream. Notably useful for conversion operations. A stream
 * created this way doesn't need to `commit` afterwards, since it's not
 * writable,
 * except for Variable Size Fields streams,
 * since setting the array of Field Sizes is a separate operation.
 */
ZL_Output* ENC_refTypedStream(
        ZL_Encoder* eictx,
        int outcomeIndex,
        size_t eltWidth,
        size_t eltCount,
        ZL_Input const* ref,
        size_t offsetBytes);

/* private access to private param */
const void* ENC_getPrivateParam(const ZL_Encoder* ei);

/* ========================================================================
 * API exploration area
 *
 * Todo : Transform accepting N inputs, with all inputs of type serialized:
 *
 */

ZL_END_C_DECLS

#endif // ZSTRONG_COMPRESS_CBINDING_H
