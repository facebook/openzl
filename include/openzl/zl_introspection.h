// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_ZL_INTROSPECTION_H
#define OPENZL_ZL_INTROSPECTION_H

#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_localParams.h"
#include "openzl/zl_opaque_types.h"

// Forward declaration to avoid circular dependency with zl_graph_api.h
typedef struct ZL_GraphParameters_s ZL_RuntimeGraphParameters;

// Introspection hooks for compress
typedef struct ZL_CompressIntrospectionHooks_s {
    void* opaque; // an opaque pointer, passed as-is to all the hooks as the
    // first argument

    /* ******** Segmenter API methods ******** */
    void (*on_segmenterEncode_start)(
            void* opaque,
            ZL_Segmenter* segCtx,
            void* placeholder) ZL_NOEXCEPT_FUNC_PTR;
    void (*on_segmenterEncode_end)(
            void* opaque,
            ZL_Segmenter* segCtx,
            ZL_Report r) ZL_NOEXCEPT_FUNC_PTR;
    void (*on_ZL_Segmenter_processChunk_start)(
            void* opaque,
            ZL_Segmenter* segCtx,
            const size_t numElts[],
            size_t numInputs,
            ZL_GraphID startingGraphID,
            const ZL_RuntimeGraphParameters* rGraphParams) ZL_NOEXCEPT_FUNC_PTR;
    void (*on_ZL_Segmenter_processChunk_end)(
            void* opaque,
            ZL_Segmenter* segCtx,
            ZL_Report r) ZL_NOEXCEPT_FUNC_PTR;

    /* ******** Encoder API methods ******** */
    void (*on_ZL_Encoder_getScratchSpace)(
            void* opaque,
            ZL_Encoder* ei,
            size_t size) ZL_NOEXCEPT_FUNC_PTR;
    void (*on_ZL_Encoder_sendCodecHeader)(
            void* opaque,
            ZL_Encoder* eictx,
            const void* trh,
            size_t trhSize) ZL_NOEXCEPT_FUNC_PTR;
    void (*on_ZL_Encoder_createTypedStream)(
            void* opaque,
            ZL_Encoder* eic,
            int outStreamIndex,
            size_t eltsCapacity,
            size_t eltWidth,
            ZL_Output* createdStream) ZL_NOEXCEPT_FUNC_PTR;

    /* ******** Graph API methods ******** */
    void (*on_ZL_Graph_getScratchSpace)(
            void* opaque,
            ZL_Graph* gctx,
            size_t size) ZL_NOEXCEPT_FUNC_PTR;
    void (*on_ZL_Edge_setMultiInputDestination_wParams)(
            void* opaque,
            ZL_Graph* gctx,
            ZL_Edge* inputs[],
            size_t nbInputs,
            ZL_GraphID gid,
            const ZL_LocalParams* lparams) ZL_NOEXCEPT_FUNC_PTR;

    /* ******** CCtx Internals ******** */
    void (*on_migraphEncode_start)(
            void* opaque,
            ZL_Graph* gctx,
            const ZL_Compressor* compressor,
            ZL_GraphID gid,
            ZL_Edge* inputs[],
            size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR;
    void (*on_migraphEncode_end)(
            void* opaque,
            ZL_Graph*,
            ZL_GraphID successorGraphs[],
            size_t nbSuccessors,
            ZL_Report graphExecResult) ZL_NOEXCEPT_FUNC_PTR;
    void (*on_codecEncode_start)(
            void* opaque,
            ZL_Encoder* eictx,
            const ZL_Compressor* compressor,
            ZL_NodeID nid,
            const ZL_Input* inStreams[],
            size_t nbInStreams) ZL_NOEXCEPT_FUNC_PTR;
    void (*on_codecEncode_end)(
            void* opaque,
            ZL_Encoder*,
            const ZL_Output* outStreams[],
            size_t nbOutputs,
            ZL_Report codecExecResult) ZL_NOEXCEPT_FUNC_PTR;
    void (*on_cctx_convertOneInput)(
            void* opque,
            const ZL_CCtx* const cctx,
            const ZL_Data* const input,
            const ZL_Type inType,
            const ZL_Type portTypeMask,
            const ZL_Report conversionResult) ZL_NOEXCEPT_FUNC_PTR;

    /* ******** CCtx entrypoint ******** */
    void (*on_ZL_CCtx_compressMultiTypedRef_start)(
            void* opaque,
            ZL_CCtx const* const cctx,
            void const* const dst,
            size_t const dstCapacity,
            ZL_TypedRef const* const inputs[],
            size_t const nbInputs) ZL_NOEXCEPT_FUNC_PTR;
    void (*on_ZL_CCtx_compressMultiTypedRef_end)(
            void* opaque,
            ZL_CCtx const* const cctx,
            ZL_Report const result) ZL_NOEXCEPT_FUNC_PTR;
} ZL_CompressIntrospectionHooks;

#endif // OPENZL_ZL_INTROSPECTION_H
