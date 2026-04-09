// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_DECOMPRESS_DCTX2_H
#define ZSTRONG_DECOMPRESS_DCTX2_H

#include "openzl/common/wire_format.h"            // PublicTransformInfo
#include "openzl/decompress/decode_frameheader.h" // DFH_Struct
#include "openzl/shared/portability.h"
#include "openzl/zl_data.h"         // ZL_Type
#include "openzl/zl_decompress.h"   // ZL_DCtx
#include "openzl/zl_opaque_types.h" // ZL_IDType

ZL_BEGIN_C_DECLS

/* DCTX_newStream():
 * Create a new stream
 * and reserve a buffer for it,
 * respecting the requested capacity.
 * The stream type must match the transform's declaration.
 */
ZL_Data* DCTX_newStream(
        ZL_DCtx* dctx,
        ZL_IDType streamID,
        ZL_Type st,
        size_t eltWidth,
        size_t eltsCapacity);

/* DCTX_newStreamFromConstRef():
 * Create a new stream
 * and make it a read-only reference to given content starting at @rPtr.
 * The content shall outlive the stream's lifetime, e.g. from the frame.
 * The stream type must match the transform's declaration.
 */
ZL_Data* DCTX_newStreamFromConstRef(
        ZL_DCtx* dctx,
        ZL_IDType streamID,
        ZL_Type st,
        size_t eltWidth,
        size_t nbElts,
        const void* rPtr);

/* DCTX_newStreamFromStreamRef():
 * Create a new stream
 * and make it a read-only reference to @ref stream
 * starting at @offsetBytes.
 * The stream type must match the transform's declaration.
 */
ZL_Data* DCTX_newStreamFromStreamRef(
        ZL_DCtx* dctx,
        ZL_IDType streamID,
        ZL_Type st,
        size_t eltWidth,
        size_t nbElts,
        ZL_Data const* ref,
        size_t offsetBytes);

size_t ZL_DCtx_getNumStreams(const ZL_DCtx* dctx);

const ZL_Data* ZL_DCtx_getConstStream(const ZL_DCtx* dctx, ZL_IDType streamID);

/**
 * @pre Can only be called during an active decompression,
 * after the frame header has been decoded.
 * @returns The ZStrong format version of the frame which
 * is currently being decompressed.
 */
unsigned ZL_DCtx_getFrameFormatVersion(const ZL_DCtx* dctx);

/****************************************************
 * Benchmarking & analysis functions
 *
 * These functions allow benchmarking and analysis to
 * hook into the ZL_DCtx and poke at it. They should
 * not be used for the main decompression workflow.
 ***************************************************/

/* DCTX_preserveStreams():
 * Tell the dctx to preserve the output streams after decompression.
 * This is useful for stream analysis, benchmarking, etc.
 * TODO: Consider making this a global parameter once supported.
 */
void DCTX_preserveStreams(ZL_DCtx* dctx);

/* DCTX_runTransformID():
 * Only used for specific benchmark scenarios.
 * Requires DCTX_preserveStreams() to be enabled.
 * After ZL_DCtx_decompress(), scans through the produced streams
 * and run only the given transform ID.
 * @returns The total number of bytes produced or an error.
 */
ZL_Report DCTX_runTransformID(ZL_DCtx* dctx, ZL_IDType transformID);

/**
 * @returns The decoded frame header or NULL on an error.
 * NOTE: This must be called after a decoding operation.
 */
DFH_Struct const* DCtx_getFrameHeader(ZL_DCtx const* dctx);

/**
 * @returns The number of input streams the decoder at decoderIdx has.
 * This includes both singleton and variable output streams.
 *
 * The decoderIdx can be retrieved from the frame header.
 * Note : used by streamdump2
 */
ZL_Report DCtx_getNbInputStreams(ZL_DCtx const* dctx, ZL_IDType decoderIdx);

/**
 * @returns The name of a transform.
 * If a transform exist but its name is not set, @returns "" empty string.
 *
 * If the request is bogus (incorrect decoderIdx), @returns NULL.
 * Note : used by streamdump2
 */
const char* DCTX_getTrName(ZL_DCtx const* dctx, ZL_IDType decoderIdx);

/**
 * @returns current memory usage for Streams managed by @dctx
 */
size_t DCTX_streamMemory(ZL_DCtx const* dctx);

// Finalize Global Parameter values for current decompression session
// Priority order is : dctx.RequestedParams > default
ZL_Report DCtx_setAppliedParameters(ZL_DCtx* dctx);

int DCtx_getAppliedGParam(const ZL_DCtx* dctx, ZL_DParam gdparam);

/// Sentinel value indicating a stream is stored in the frame rather than
/// produced by a decoder node.
#define ZL_PRODUCER_STORE ((ZL_IDType)(-1))

typedef struct ZL_AppendToOutputOptimization_s ZL_AppendToOutputOptimization;

typedef struct ZL_DecoderFusionDesc_s ZL_DecoderFusionDesc;

/// Metadata about a single data stream in the decompression context.
typedef struct ZL_DCtx_DataInfo {
    ZL_Data* data;
    ZL_AppendToOutputOptimization* appendOpt;
    /// The index of the node that produced this stream or
    /// ZL_PRODUCER_STORE if stored in the frame.
    ZL_IDType producerNodeIdx;
} ZL_DCtx_DataInfo;

/**
 * Run the decoder for a single node.
 *
 * @param nodeInfo The node to run.
 * @param withinFusedDecoder If true, this function is being called from within
 * a currently executing decoder fusion via ZL_DecoderFusion_runCodec(), so do
 * not search for decoder fusions as the caller is explicitly asking for the
 * codec to be executed.
 */
ZL_Report DCTX_runDecoder(
        ZL_DCtx* dctx,
        const DFH_NodeInfo* nodeInfo,
        bool withinFusedDecoder);

ZL_END_C_DECLS

#endif // ZSTRONG_DECOMPRESS_DCTX2_H
