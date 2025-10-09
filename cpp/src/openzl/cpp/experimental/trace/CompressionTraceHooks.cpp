// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/cpp/experimental/trace/CompressionTraceHooks.hpp"

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/logging.h"
#include "openzl/compress/dyngraph_interface.h"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/experimental/trace/Codec.hpp"
#include "openzl/cpp/experimental/trace/StreamVisualizer.hpp"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_input.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_output.h"
#include "openzl/zl_reflection.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openzl::visualizer {

inline std::string streamTypeToStr(ZL_Type stype)
{
    switch (stype) {
        case ZL_Type_serial:
            return "Serialized";
        case ZL_Type_struct:
            return "Fixed_Width";
        case ZL_Type_numeric:
            return "Numeric";
        case ZL_Type_string:
            return "Variable_Size";
        default:
            return "default";
    }
}

inline std::string graphTypeToStr(ZL_GraphType gtype)
{
    switch (gtype) {
        case ZL_GraphType_standard:
            return "Standard";
        case ZL_GraphType_static:
            return "Static";
        case ZL_GraphType_selector:
            return "Selector";
        case ZL_GraphType_function:
            return "Function";
        case ZL_GraphType_multiInput:
            return "Multiple_Input";
        case ZL_GraphType_parameterized:
            return "Parameterized";
        case ZL_GraphType_segmenter:
            return "Segmenter";
        default:
            throw std::runtime_error("Unsupported ZL_GraphType value!");
    }
}

void CompressionTraceHooks::on_codecEncode_start(
        ZL_Encoder* encoder,
        const ZL_Compressor* compressor,
        ZL_NodeID nid,
        const ZL_Input* inStreams[],
        size_t nbInStreams)
{
    // Trampoline to Tracer
    tracer_->on_codecEncode_start(
            encoder, compressor, nid, inStreams, nbInStreams);
}

void CompressionTraceHooks::on_codecEncode_end(
        ZL_Encoder* eictx,
        const ZL_Output* outStreams[],
        size_t nbOutputs,
        ZL_Report codecExecResult)
{
    // Trampoline to Tracer
    tracer_->on_codecEncode_end(eictx, outStreams, nbOutputs, codecExecResult);
}

void CompressionTraceHooks::on_ZL_Encoder_getScratchSpace(ZL_Encoder*, size_t)
{
}

void CompressionTraceHooks::on_ZL_Encoder_sendCodecHeader(
        ZL_Encoder* eictx,
        const void* trh,
        size_t trhSize)
{
    // Trampoline to Tracer
    tracer_->on_ZL_Encoder_sendCodecHeader(eictx, trh, trhSize);
}

void CompressionTraceHooks::on_ZL_Encoder_createTypedStream(
        ZL_Encoder*,
        int,
        size_t eltsCapacity,
        size_t eltWidth,
        ZL_Output* createdStream)
{
}

void CompressionTraceHooks::on_migraphEncode_start(
        ZL_Graph* graph,
        const ZL_Compressor* compressor,
        ZL_GraphID gid,
        ZL_Edge* edges[],
        size_t nbEdges)
{
    // Trampoline to Tracer
    tracer_->on_migraphEncode_start(graph, compressor, gid, edges, nbEdges);
}

void CompressionTraceHooks::on_migraphEncode_end(
        ZL_Graph* gctx,
        ZL_GraphID ssuccesorGraphs[],
        size_t nbSuccessors,
        ZL_Report graphExecResult)
{
    // Trampoline to Tracer
    tracer_->on_migraphEncode_end(
            gctx, ssuccesorGraphs, nbSuccessors, graphExecResult);
}

void CompressionTraceHooks::on_cctx_convertOneInput(
        const ZL_CCtx* const cctx,
        const ZL_Data* const input,
        const ZL_Type inType,
        const ZL_Type portTypeMask,
        const ZL_Report conversionResult)
{
    // Trampoline to Tracer
    tracer_->on_cctx_convertOneInput(
            cctx, input, inType, portTypeMask, conversionResult);
}

void CompressionTraceHooks::on_ZL_Graph_getScratchSpace(ZL_Graph*, size_t) {}

void CompressionTraceHooks::on_ZL_Edge_setMultiInputDestination_wParams(
        ZL_Graph*,
        ZL_Edge*[],
        size_t,
        ZL_GraphID,
        const ZL_LocalParams*)
{
}

void CompressionTraceHooks::on_ZL_CCtx_compressMultiTypedRef_start(
        ZL_CCtx const* const cctx,
        void const* const dst,
        size_t const dstCapacity,
        ZL_TypedRef const* const inputs[],
        size_t const nbInputs)
{
    // Reset the output stream
    outStream_.str("");
    outStream_.clear();
    latestStreamdumpCache_ =
            std::map<size_t, std::pair<std::string, std::string>>();

    if (tracer_) {
        throw std::runtime_error(
                "Corrupted state. Trace context already exists!");
    }
    tracer_ = std::make_unique<Tracer>(cctx);
    tracer_->on_ZL_CCtx_compressMultiTypedRef_start(
            cctx, dst, dstCapacity, inputs, nbInputs);
}

void CompressionTraceHooks::on_ZL_CCtx_compressMultiTypedRef_end(
        ZL_CCtx const* const cctx,
        ZL_Report const result)
{
    tracer_->on_ZL_CCtx_compressMultiTypedRef_end(cctx, result);

    auto trace             = tracer_->extractTrace();
    latestTraceCache_      = std::move(trace.trace);
    latestStreamdumpCache_ = std::move(trace.streamdump);
    tracer_                = nullptr;
}

std::pair<
        poly::string_view,
        std::map<size_t, std::pair<poly::string_view, poly::string_view>>>
CompressionTraceHooks::getLatestTrace()
{
    std::map<size_t, std::pair<poly::string_view, poly::string_view>>
            streamdumps;
    for (auto& [k, v] : latestStreamdumpCache_) {
        streamdumps[k] = { poly::string_view(v.first),
                           poly::string_view(v.second) };
    }
    return { latestTraceCache_, std::move(streamdumps) };
}

} // namespace openzl::visualizer
