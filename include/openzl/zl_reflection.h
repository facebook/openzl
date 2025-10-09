// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * zs2_reflection.h
 *
 * This API provides information about ZStrong nodes and graphs.
 */

#ifndef ZSTRONG_ZL_REFLECTION_H
#define ZSTRONG_ZL_REFLECTION_H

#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_portability.h" // ZL_NOEXCEPT_FUNC_PTR

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum {
    ZL_GraphType_standard,
    ZL_GraphType_static,
    ZL_GraphType_selector,
    ZL_GraphType_function,
    ZL_GraphType_multiInput,
    ZL_GraphType_parameterized,
    ZL_GraphType_segmenter,
} ZL_GraphType;

typedef ZL_Report (*ZL_Compressor_ForEachGraphCallback)(
        void* opaque,
        const ZL_Compressor* compressor,
        ZL_GraphID graphID) ZL_NOEXCEPT_FUNC_PTR;

/**
 * Calls @p callback on every graph registered in the @p compressor.
 * If @p callback returns an error, short-circuit and return that error.
 * @returns Success if all callbacks succeed, or the first error.
 * @note The callback will not be called on standard graphs, since they
 * aren't registered in the @p compressor.
 */
ZL_Report ZL_Compressor_forEachGraph(
        const ZL_Compressor* compressor,
        ZL_Compressor_ForEachGraphCallback callback,
        void* opaque);

typedef ZL_Report (*ZL_Compressor_ForEachNodeCallback)(
        void* opaque,
        const ZL_Compressor* compressor,
        ZL_NodeID graphID) ZL_NOEXCEPT_FUNC_PTR;

/**
 * Calls @p callback on every node registered in the @p compressor.
 * If @p callback returns an error, short-circuit and return that error.
 * @returns Success if all callbacks succeed, or the first error.
 * @note The callback will not be called on standard nodes, since they
 * aren't registered in the @p compressor.
 */
ZL_Report ZL_Compressor_forEachNode(
        const ZL_Compressor* compressor,
        ZL_Compressor_ForEachNodeCallback callback,
        void* opaque);

typedef ZL_Report (*ZL_Compressor_ForEachParamCallback)(
        void* opaque,
        ZL_CParam param,
        int val) ZL_NOEXCEPT_FUNC_PTR;

/**
 * Calls @p callback on every param set in the @p compressor.
 * If @p callback returns an error, short-circuit and return that error.
 * @returns Success if all callbacks succeed, or the first error.
 * @note The callback will not be called on params that are equal to
 * the default value (0).
 */
ZL_Report ZL_Compressor_forEachParam(
        const ZL_Compressor* compressor,
        ZL_Compressor_ForEachParamCallback callback,
        void* opaque);

/**
 * @param[out] graphID If the function returns true, filled with
 * the starting GraphID, otherwise filled with ZL_GRAPH_ILLEGAL.
 * @returns True iff the compressor has the starting GraphID set.
 */
bool ZL_Compressor_getStartingGraphID(
        const ZL_Compressor* compressor,
        ZL_GraphID* graphID);

/**
 * @returns The @ref ZL_GraphType of the given graph.
 * @note This is the original type the graph was registered with.
 */
ZL_GraphType ZL_Compressor_getGraphType(
        const ZL_Compressor* compressor,
        ZL_GraphID graph);

/**
 * @returns the (optional) @p name given to this graphid.
 * When none provided, returns "".
 * When graphid is illegal (invalid), returns NULL.
 */
const char* ZL_Compressor_Graph_getName(
        ZL_Compressor const* cgraph,
        ZL_GraphID graphid);

/**
 * @returns The input stream types that @p graphid is compatible with.
 * @pre @p graph must be valid, and must have a single input stream
 */
ZL_Type ZL_Compressor_Graph_getInput0Mask(
        ZL_Compressor const* cgraph,
        ZL_GraphID graphid);

/**
 * @returns The input stream types that @p graphid input @p inputIdx is
 * compatible with.
 */
ZL_Type ZL_Compressor_Graph_getInputMask(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid,
        size_t inputIdx);

/**
 * @returns The number of input streams that @p graphid expects.
 */
size_t ZL_Compressor_Graph_getNumInputs(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid);

/**
 * @returns True if @p graphid is a multi input graph whose last
 * input may show up zero or more times.
 */
bool ZL_Compressor_Graph_isVariableInput(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid);

/**
 * @returns The head node of the graph if @p graphid is a static graph,
 * otherwise returns ZL_NODE_ILLEGAL.
 */
ZL_NodeID ZL_Compressor_Graph_getHeadNode(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid);

/**
 * @returns If the provided @p graphid was created by modifying another
 *          existing graph, i.e., its type is `ZL_GraphType_parameterized`,
 *          the `ZL_GraphID` of that other graph. Otherwise, it returns
 *          `ZL_GRAPH_ILLEGAL`.
 */
ZL_GraphID ZL_Compressor_Graph_getBaseGraphID(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid);

/**
 * @returns The successor of the head node in the graph if @p graphid is a
 * static graph, otherwise returns an empty list.
 * @note The array is valid for the lifetime of the @p compressor.
 */
ZL_GraphIDList ZL_Compressor_Graph_getSuccessors(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid);

/**
 * @returns The custom nodes of @p graphid, which is only non-empty for
 * dynamic and multi-input graphs.
 * @note The array is valid for the lifetime of the @p compressor.
 */
ZL_NodeIDList ZL_Compressor_Graph_getCustomNodes(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid);

/**
 * @returns The custom graphs of @p graphid, which is only non-empty for
 * selector, dynamic, and multi-input graphs.
 * @note The array is valid for the lifetime of the @p compressor.
 */
ZL_GraphIDList ZL_Compressor_Graph_getCustomGraphs(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid);

/**
 * @returns The local params of @p graphid.
 * @note The params for the lifetime of the @p compressor.
 */
ZL_LocalParams ZL_Compressor_Graph_getLocalParams(
        const ZL_Compressor* compressor,
        ZL_GraphID graphid);

/* ########    Nodes    ######## */

/**
 * @returns The number of input streams that @p node expects
 * in the context of the @p cgraph.
 */
size_t ZL_Compressor_Node_getNumInputs(
        ZL_Compressor const* cgraph,
        ZL_NodeID node);

/**
 * @returns The input stream type that @p node expects
 * @pre @p node must be valid, and must have a single input stream
 */
ZL_Type ZL_Compressor_Node_getInput0Type(
        ZL_Compressor const* cgraph,
        ZL_NodeID node);

/**
 * @returns The input stream type that @p node has at index @p inputIndex in
 * the context of the @p cgraph
 *
 * @pre @p inputIndex is less than #ZL_Compressor_Node_getNumInputs(@p cgraph ,
 * @p node). It is an error to pass an @p inputIndex greater or equal to the
 * number of inputs. Note that all input streams within a group of variable
 * inputs share the same @p inputIndex.
 */
ZL_Type ZL_Compressor_Node_getInputType(
        ZL_Compressor const* cgraph,
        ZL_NodeID nodeid,
        ZL_IDType inputIndex);

/**
 * @returns True if @p nodeid is a multi input codec whose last
 * input may show up zero or more times.
 */
bool ZL_Compressor_Node_isVariableInput(
        const ZL_Compressor* compressor,
        ZL_NodeID nodeid);

/**
 * @returns The number of output outcomes that @p node has
 * in the context of the @p cgraph.
 */
size_t ZL_Compressor_Node_getNumOutcomes(
        ZL_Compressor const* cgraph,
        ZL_NodeID node);

/**
 * @returns The number of variable output that @p node has
 * in the context of the @p cgraph.
 */
size_t ZL_Compressor_Node_getNumVariableOutcomes(
        ZL_Compressor const* cgraph,
        ZL_NodeID node);

/**
 * @returns The output stream type that @p node has at index @p outputIndex in
 * the context of the @p cgraph
 *
 * @pre @p outputIndex is less than #ZL_Compressor_Node_getNbOutputs(@p cgraph
 * , @p node)
 */
ZL_Type ZL_Compressor_Node_getOutputType(
        ZL_Compressor const* cgraph,
        ZL_NodeID node,
        int outputIndex);

/**
 * @returns The local params of @p nodeid.
 * @note The params for the lifetime of the @p compressor.
 */
ZL_LocalParams ZL_Compressor_Node_getLocalParams(
        const ZL_Compressor* cgraph,
        ZL_NodeID nodeid);

/**
 * @returns the max format version that @p node supports
 * in the context of the @p cgraph.
 */
unsigned ZL_Compressor_Node_getMaxVersion(
        ZL_Compressor const* cgraph,
        ZL_NodeID node);

/**
 * @returns the min format version that @p node supports
 * in the context of the @p cgraph.
 */
unsigned ZL_Compressor_Node_getMinVersion(
        ZL_Compressor const* cgraph,
        ZL_NodeID node);

/**
 * @returns The transform ID for the node, which is the ID that is written into
 * the frame header, and how we determine which decoder to use.
 */
ZL_IDType ZL_Compressor_Node_getCodecID(
        ZL_Compressor const* cgraph,
        ZL_NodeID node);

/**
 * @returns If the provided @p node was created by modifying another existing
 * node, the `ZL_NodeID` of that other node. Otherwise, `ZL_NODE_ILLEGAL`.
 */
ZL_NodeID ZL_Compressor_Node_getBaseNodeID(
        ZL_Compressor const* cgraph,
        ZL_NodeID node);

/**
 * @returns The name of the node if available, else "", and returns NULL if @p
 * node is invalid.
 */
char const* ZL_Compressor_Node_getName(
        ZL_Compressor const* cgraph,
        ZL_NodeID node);

/**
 * @returns a boolean value indicating whether the node is standard or not.
 */
bool ZL_Compressor_Node_isStandard(ZL_Compressor const* cgraph, ZL_NodeID node);

/**
 * Reflection API for introspecting a compressed frame.
 *
 * 1. Create a reflection context with ZL_ReflectionCtx_create().
 * 2. Register the transforms used in the frame with
 *    ZL_ReflectionCtx_registerTypedDecoder() and friends.
 * 3. Set the compressed frame to process with
 *    ZL_ReflectionCtx_setCompressedFrame().
 * 4. Get the information you need with the various
 * ZL_ReflectionCtx_get*()
 * 5. Free the context with ZL_ReflectionCtx_free().
 *
 * NOTE: This API is guaranteed to be safe on corrupted input.
 * WARNING: Logic errors in the API usage will crash the process.
 * For example trying to call any getter (e.g.
 * ZL_ReflectionCtx_getNumStreams_lastChunk()) before calling
 * ZL_ReflectionCtx_setCompressedFrame() and getting a successful return
 * code.
 */

typedef struct ZL_ReflectionCtx_s ZL_ReflectionCtx;

typedef struct ZL_DataInfo_s ZL_DataInfo;

typedef struct ZL_CodecInfo_s ZL_CodecInfo;

ZL_ReflectionCtx* ZL_ReflectionCtx_create(void);
void ZL_ReflectionCtx_free(ZL_ReflectionCtx* rctx);

/**
 * @returns The dctx that will be used for decompression so the user
 * can set parameters and register custom transforms if the helpers
 * provided aren't sufficient.
 *
 * @pre Must be called before ZL_ReflectionCtx_setCompressedFrame().
 */
ZL_DCtx* ZL_ReflectionCtx_getDCtx(ZL_ReflectionCtx* rctx);

/**
 * Registers a custom transform to be used when decoding the frame.
 * @pre Must be called before ZL_ReflectionCtx_setCompressedFrame().
 */
void ZL_ReflectionCtx_registerTypedDecoder(
        ZL_ReflectionCtx* rctx,
        ZL_TypedDecoderDesc const* dtd);

/**
 * Registers a custom transform to be used when decoding the frame.
 * @pre Must be called before ZL_ReflectionCtx_setCompressedFrame().
 */
void ZL_ReflectionCtx_registerVODecoder(
        ZL_ReflectionCtx* rctx,
        ZL_VODecoderDesc const* dtd);

/**
 * Registers a custom transform to be used when decoding the frame.
 * @pre Must be called before ZL_ReflectionCtx_setCompressedFrame().
 */
void ZL_ReflectionCtx_registerMIDecoder(
        ZL_ReflectionCtx* rctx,
        ZL_MIDecoderDesc const* dtd);

/**
 * Decompresses the frame in @p src and build all the information needed to
 * introspect the frame.
 *
 * @pre Must only be called once per @p rctx.
 *
 * @returns ZL_returnSuccess() if decompression sueeeded.
 */
ZL_Report ZL_ReflectionCtx_setCompressedFrame(
        ZL_ReflectionCtx* rctx,
        void const* src,
        size_t srcSize);

/**
 * @returns The frame format version.
 * @pre ZL_ReflectionCtx_setCompressedFrame() returned success.
 */
uint32_t ZL_ReflectionCtx_getFrameFormatVersion(ZL_ReflectionCtx const* rctx);

/**
 * @returns The size of the frame header.
 * @pre ZL_ReflectionCtx_setCompressedFrame() returned success.
 */
size_t ZL_ReflectionCtx_getFrameHeaderSize(ZL_ReflectionCtx const* rctx);

/**
 * @returns The size of the frame footer.
 * @pre ZL_ReflectionCtx_setCompressedFrame() returned success.
 */
size_t ZL_ReflectionCtx_getFrameFooterSize(ZL_ReflectionCtx const* rctx);

/**
 * @returns The total size of all the transform headers.
 * @pre ZL_ReflectionCtx_setCompressedFrame() returned success.
 */
size_t ZL_ReflectionCtx_getTotalTransformHeaderSize_lastChunk(
        ZL_ReflectionCtx const* rctx);

/**
 * @returns The total number of streams in the decoder graph. This includes
 * streams stored in the frame, input streams to the frame, and intermediate
 * streams.
 * @pre ZL_ReflectionCtx_setCompressedFrame() returned success.
 */
size_t ZL_ReflectionCtx_getNumStreams_lastChunk(ZL_ReflectionCtx const* rctx);

/**
 * @returns The stream at @p index
 * @pre ZL_ReflectionCtx_setCompressedFrame() returned success.
 * @pre @p index is valid.
 */
ZL_DataInfo const* ZL_ReflectionCtx_getStream_lastChunk(
        ZL_ReflectionCtx const* rctx,
        size_t index);

/**
 * @returns The number of input streams to the compressed frame.
 * @pre ZL_ReflectionCtx_setCompressedFrame() returned success.
 */
size_t ZL_ReflectionCtx_getNumInputs(ZL_ReflectionCtx const* rctx);

/**
 * @returns The input stream at @p index
 * @pre ZL_ReflectionCtx_setCompressedFrame() returned success.
 * @pre @p index is valid.
 */
ZL_DataInfo const* ZL_ReflectionCtx_getInput(
        ZL_ReflectionCtx const* rctx,
        size_t index);

/**
 * @returns The number of streams stored in the compressed frame.
 * @pre ZL_ReflectionCtx_setCompressedFrame() returned success.
 */
size_t ZL_ReflectionCtx_getNumStoredOutputs_lastChunk(
        ZL_ReflectionCtx const* rctx);

/**
 * @returns The stored stream at @p index
 * @pre ZL_ReflectionCtx_setCompressedFrame() returned success.
 * @pre @p index is valid.
 */
ZL_DataInfo const* ZL_ReflectionCtx_getStoredOutput_lastChunk(
        ZL_ReflectionCtx const* rctx,
        size_t index);

/**
 * @returns the number of transforms that are run during the decoding process.
 * @pre ZL_ReflectionCtx_setCompressedFrame() returned success.
 */
size_t ZL_ReflectionCtx_getNumCodecs_lastChunk(ZL_ReflectionCtx const* rctx);

/**
 * @returns The transform at @p index
 * @pre ZL_ReflectionCtx_setCompressedFrame() returned success.
 * @pre @p index is valid.
 */
ZL_CodecInfo const* ZL_ReflectionCtx_getCodec_lastChunk(
        ZL_ReflectionCtx const* rctx,
        size_t index);

/**
 * @returns The type of the stream.
 */
ZL_Type ZL_DataInfo_getType(ZL_DataInfo const* si);

/**
 * @returns The number of elements in the stream.
 */
size_t ZL_DataInfo_getNumElts(ZL_DataInfo const* si);

/**
 * @returns The element width of the stream.
 */
size_t ZL_DataInfo_getEltWidth(ZL_DataInfo const* si);

/**
 * @returns The content size of the stream.
 * For non-string streams equal to nbElts * eltWidth.
 * For string streams equal to the sum of the lengths.
 */
size_t ZL_DataInfo_getContentSize(ZL_DataInfo const* si);

/**
 * @returns The index for which ZL_ReflectionCtx_getStream_lastChunk() returns
 * this stream.
 */
size_t ZL_DataInfo_getIndex(ZL_DataInfo const* si);

/**
 * @returns The data pointer for the stream.
 */
void const* ZL_DataInfo_getDataPtr(ZL_DataInfo const* si);

/**
 * @returns The length pointer for the stream if the type is
 * ZL_Type_string.
 * @pre The type is ZL_Type_string.
 */
uint32_t const* ZL_DataInfo_getLengthsPtr(ZL_DataInfo const* si);

/**
 * @returns The transform that produced this stream or NULL
 * if the stream is stored in the frame.
 */
ZL_CodecInfo const* ZL_DataInfo_getProducerCodec(ZL_DataInfo const* si);

/**
 * @returns The transform that consumes this stream or NULL
 * if the stream is an input stream to the frame.
 */
ZL_CodecInfo const* ZL_DataInfo_getConsumerCodec(ZL_DataInfo const* si);

/**
 * @returns The name of the transform
 */
char const* ZL_CodecInfo_getName(ZL_CodecInfo const* ti);

/**
 * @returns The transform ID
 */
ZL_IDType ZL_CodecInfo_getCodecID(ZL_CodecInfo const* ti);

/**
 * @returns true iff the transform is a standard transform.
 */
bool ZL_CodecInfo_isStandardCodec(ZL_CodecInfo const* ti);

/**
 * @returns true iff the transform is a custom transform.
 */
bool ZL_CodecInfo_isCustomCodec(ZL_CodecInfo const* ti);

/**
 * @returns The index for which ZL_ReflectionCtx_getCodec_lastChunk() returns
 * this transform.
 */
size_t ZL_CodecInfo_getIndex(ZL_CodecInfo const* ti);

/**
 * @returns The header pointer for the transform.
 */
void const* ZL_CodecInfo_getHeaderPtr(ZL_CodecInfo const* ti);

/**
 * @returns The header size of the transform.
 */
size_t ZL_CodecInfo_getHeaderSize(ZL_CodecInfo const* ti);

/**
 * @returns The number of input streams to the transform.
 * Input streams are streams that are consumed by the encoder and produced by
 * the decoder.
 */
size_t ZL_CodecInfo_getNumInputs(ZL_CodecInfo const* ti);

/**
 * @returns The input stream of the transform at index @p index
 * @pre index is valid
 */
ZL_DataInfo const* ZL_CodecInfo_getInput(ZL_CodecInfo const* ti, size_t index);

/**
 * @returns The number of output streams to the transform.
 * Output streams are streams that are produced by the encoder and consumed by
 * the decoder.
 */
size_t ZL_CodecInfo_getNumOutputs(ZL_CodecInfo const* ti);

/**
 * @returns The output stream of the transform at index @p index
 * @pre index is valid
 */
ZL_DataInfo const* ZL_CodecInfo_getOutput(ZL_CodecInfo const* ti, size_t index);

/**
 * @returns The number of output streams of the transform that are
 * variable outputs. Necessarily no greater than
 * ZL_CodecInfo_getOutput().
 */
size_t ZL_CodecInfo_getNumVariableOutputs(ZL_CodecInfo const* ti);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif
