// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/CompressIntrospectionHooks.hpp"

namespace openzl {

CompressIntrospectionHooks::CompressIntrospectionHooks()
{
    rawHooks_.opaque = this;

    rawHooks_.on_segmenterEncode_start =
            [](void* this_ptr, ZL_Segmenter* segCtx, void*) noexcept {
                ((CompressIntrospectionHooks*)this_ptr)
                        ->on_segmenterEncode_start(segCtx);
            };
    rawHooks_.on_segmenterEncode_end =
            [](void* this_ptr, ZL_Segmenter* segCtx, ZL_Report r) noexcept {
                ((CompressIntrospectionHooks*)this_ptr)
                        ->on_segmenterEncode_end(segCtx, r);
            };
    rawHooks_.on_ZL_Segmenter_processChunk_start =
            [](void* this_ptr,
               ZL_Segmenter* segCtx,
               const size_t numElts[],
               size_t numInputs,
               ZL_GraphID startingGraphID,
               const ZL_RuntimeGraphParameters* rGraphParams) noexcept {
                ((CompressIntrospectionHooks*)this_ptr)
                        ->on_ZL_Segmenter_processChunk_start(
                                segCtx,
                                numElts,
                                numInputs,
                                startingGraphID,
                                rGraphParams);
            };

    rawHooks_.on_ZL_Segmenter_processChunk_end =
            [](void* this_ptr, ZL_Segmenter* segCtx, ZL_Report r) noexcept {
                ((CompressIntrospectionHooks*)this_ptr)
                        ->on_ZL_Segmenter_processChunk_end(segCtx, r);
            };

    rawHooks_.on_codecEncode_start = [](void* this_ptr,
                                        ZL_Encoder* eictx,
                                        const ZL_Compressor* compressor,
                                        ZL_NodeID nid,
                                        const ZL_Input* inStreams[],
                                        size_t nbInStreams) noexcept {
        ((CompressIntrospectionHooks*)this_ptr)
                ->on_codecEncode_start(
                        eictx, compressor, nid, inStreams, nbInStreams);
    };
    rawHooks_.on_codecEncode_end = [](void* this_ptr,
                                      ZL_Encoder* eictx,
                                      const ZL_Output* outStreams[],
                                      size_t nbOutputs,
                                      ZL_Report codecExecResult) noexcept {
        ((CompressIntrospectionHooks*)this_ptr)
                ->on_codecEncode_end(
                        eictx, outStreams, nbOutputs, codecExecResult);
    };
    rawHooks_.on_cctx_convertOneInput = [](void* this_ptr,
                                           const ZL_CCtx* const cctx,
                                           const ZL_Data* const input,
                                           const ZL_Type inType,
                                           const ZL_Type portTypeMask,
                                           const ZL_Report
                                                   conversionResult) noexcept {
        ((CompressIntrospectionHooks*)this_ptr)
                ->on_cctx_convertOneInput(
                        cctx, input, inType, portTypeMask, conversionResult);
    };

    rawHooks_.on_ZL_Encoder_getScratchSpace =
            [](void* this_ptr, ZL_Encoder* ei, size_t size) noexcept {
                ((CompressIntrospectionHooks*)this_ptr)
                        ->on_ZL_Encoder_getScratchSpace(ei, size);
            };
    rawHooks_.on_ZL_Encoder_sendCodecHeader = [](void* this_ptr,
                                                 ZL_Encoder* eictx,
                                                 const void* trh,
                                                 size_t trhSize) noexcept {
        ((CompressIntrospectionHooks*)this_ptr)
                ->on_ZL_Encoder_sendCodecHeader(eictx, trh, trhSize);
    };
    rawHooks_.on_ZL_Encoder_createTypedStream =
            [](void* this_ptr,
               ZL_Encoder* eic,
               int outStreamIndex,
               size_t eltsCapacity,
               size_t eltWidth,
               ZL_Output* createdStream) noexcept {
                ((CompressIntrospectionHooks*)this_ptr)
                        ->on_ZL_Encoder_createTypedStream(
                                eic,
                                outStreamIndex,
                                eltsCapacity,
                                eltWidth,
                                createdStream);
            };

    rawHooks_.on_migraphEncode_start = [](void* this_ptr,
                                          ZL_Graph* gctx,
                                          const ZL_Compressor* compressor,
                                          ZL_GraphID gid,
                                          ZL_Edge* inputs[],
                                          size_t nbInputs) noexcept {
        ((CompressIntrospectionHooks*)this_ptr)
                ->on_migraphEncode_start(
                        gctx, compressor, gid, inputs, nbInputs);
    };
    rawHooks_.on_migraphEncode_end = [](void* this_ptr,
                                        ZL_Graph* gctx,
                                        ZL_GraphID successorGraphs[],
                                        size_t nbSuccessors,
                                        ZL_Report graphExecResult) noexcept {
        ((CompressIntrospectionHooks*)this_ptr)
                ->on_migraphEncode_end(
                        gctx, successorGraphs, nbSuccessors, graphExecResult);
    };
    rawHooks_.on_ZL_Graph_getScratchSpace =
            [](void* this_ptr, ZL_Graph* gctx, size_t size) noexcept {
                ((CompressIntrospectionHooks*)this_ptr)
                        ->on_ZL_Graph_getScratchSpace(gctx, size);
            };
    rawHooks_.on_ZL_Edge_setMultiInputDestination_wParams =
            [](void* this_ptr,
               ZL_Graph* gctx,
               ZL_Edge* inputs[],
               size_t nbInputs,
               ZL_GraphID gid,
               const ZL_LocalParams* lparams) noexcept {
                ((CompressIntrospectionHooks*)this_ptr)
                        ->on_ZL_Edge_setMultiInputDestination_wParams(
                                gctx, inputs, nbInputs, gid, lparams);
            };

    rawHooks_.on_ZL_CCtx_compressMultiTypedRef_start =
            [](void* this_ptr,
               ZL_CCtx* cctx,
               void const* const dst,
               size_t const dstCapacity,
               ZL_TypedRef const* const inputs[],
               size_t const nbInputs) noexcept {
                ((CompressIntrospectionHooks*)this_ptr)
                        ->on_ZL_CCtx_compressMultiTypedRef_start(
                                cctx, dst, dstCapacity, inputs, nbInputs);
            };
    rawHooks_.on_ZL_CCtx_compressMultiTypedRef_end =
            [](void* this_ptr,
               ZL_CCtx const* const cctx,
               ZL_Report const result) noexcept {
                ((CompressIntrospectionHooks*)this_ptr)
                        ->on_ZL_CCtx_compressMultiTypedRef_end(cctx, result);
            };
}

} // namespace openzl
