// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/zl_introspection.h"

namespace openzl {

class CompressIntrospectionHooks {
   public:
    CompressIntrospectionHooks();
    virtual ~CompressIntrospectionHooks() = default;

    ZL_CompressIntrospectionHooks* getRawHooks()
    {
        return &rawHooks_;
    }

    virtual void on_segmenterEncode_start(ZL_Segmenter* segCtx) {}
    virtual void on_segmenterEncode_end(ZL_Segmenter* segCtx, ZL_Report r) {}
    virtual void on_ZL_Segmenter_processChunk_start(
            ZL_Segmenter* segCtx,
            const size_t numElts[],
            size_t numInputs,
            ZL_GraphID startingGraphID,
            const ZL_RuntimeGraphParameters* rGraphParams)
    {
    }
    virtual void on_ZL_Segmenter_processChunk_end(
            ZL_Segmenter* segCtx,
            ZL_Report r)
    {
    }

    virtual void on_ZL_Encoder_getScratchSpace(ZL_Encoder* ei, size_t size) {}
    virtual void on_ZL_Encoder_sendCodecHeader(
            ZL_Encoder* eictx,
            const void* trh,
            size_t trhSize)
    {
    }
    virtual void on_ZL_Encoder_createTypedStream(
            ZL_Encoder* eic,
            int outStreamIndex,
            size_t eltsCapacity,
            size_t eltWidth,
            ZL_Output* createdStream)
    {
    }

    virtual void on_ZL_Graph_getScratchSpace(ZL_Graph* gctx, size_t size) {}
    virtual void on_ZL_Edge_setMultiInputDestination_wParams(
            ZL_Graph* gctx,
            ZL_Edge* inputs[],
            size_t nbInputs,
            ZL_GraphID gid,
            const ZL_LocalParams* lparams)
    {
    }

    virtual void on_migraphEncode_start(
            ZL_Graph* gctx,
            const ZL_Compressor* compressor,
            ZL_GraphID gid,
            ZL_Edge* inputs[],
            size_t nbInputs)
    {
    }
    virtual void on_migraphEncode_end(
            ZL_Graph*,
            ZL_GraphID successorGraphs[],
            size_t nbSuccessors,
            ZL_Report graphExecResult)
    {
    }
    virtual void on_codecEncode_start(
            ZL_Encoder* eictx,
            const ZL_Compressor* compressor,
            ZL_NodeID nid,
            const ZL_Input* inStreams[],
            size_t nbInStreams)
    {
    }
    virtual void on_codecEncode_end(
            ZL_Encoder*,
            const ZL_Output* outStreams[],
            size_t nbOutputs,
            ZL_Report codecExecResult)
    {
    }
    virtual void on_cctx_convertOneInput(
            const ZL_CCtx* const cctx,
            const ZL_Data* const,
            const ZL_Type inType,
            const ZL_Type portTypeMask,
            const ZL_Report conversionResult)
    {
    }

    virtual void on_ZL_CCtx_compressMultiTypedRef_start(
            ZL_CCtx const* const cctx,
            void const* const dst,
            size_t const dstCapacity,
            ZL_TypedRef const* const inputs[],
            size_t const nbInputs)
    {
    }
    virtual void on_ZL_CCtx_compressMultiTypedRef_end(
            ZL_CCtx const* const cctx,
            ZL_Report const result)
    {
    }

   private:
    ZL_CompressIntrospectionHooks rawHooks_{};
};

} // namespace openzl
