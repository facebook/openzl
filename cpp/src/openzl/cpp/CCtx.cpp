// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/CCtx.hpp"

#include <vector>

#include "openzl/cpp/FunctionGraph.hpp"
#include "openzl/cpp/experimental/trace/CompressionTraceHooks.hpp"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"

namespace openzl {
// TODO(terrelln): Move this?
size_t compressBound(size_t totalSize)
{
    return ZL_compressBound(totalSize);
}

CCtx::CCtx() : CCtx(ZL_CCtx_create(), ZL_CCtx_free) {}
CCtx::CCtx(ZL_CCtx* cctx, detail::NonNullUniqueCPtr<ZL_CCtx>::DeleterFn deleter)
        : cctx_(cctx, deleter)
{
}
CCtx::CCtx(CCtx&&) noexcept            = default;
CCtx& CCtx::operator=(CCtx&&) noexcept = default;
CCtx::~CCtx()                          = default;

void CCtx::setParameter(CParam param, int value)
{
    unwrap(ZL_CCtx_setParameter(get(), static_cast<ZL_CParam>(param), value));
}

int CCtx::getParameter(CParam param) const
{
    return ZL_CCtx_getParameter(get(), static_cast<ZL_CParam>(param));
}

void CCtx::resetParameters()
{
    unwrap(ZL_CCtx_resetParameters(get()));
}

void CCtx::refCompressor(const Compressor& compressor)
{
    unwrap(ZL_CCtx_refCompressor(get(), compressor.get()));
}

size_t CCtx::compress(poly::span<char> output, poly::span<const Input> inputs)
{
    if (inputs.size() == 1) {
        return unwrap(ZL_CCtx_compressTypedRef(
                get(), output.data(), output.size(), inputs[0].get()));
    }

    std::vector<const ZL_Input*> inputPtrs;
    inputPtrs.reserve(inputs.size());
    for (auto const& input : inputs) {
        inputPtrs.push_back(input.get());
    }
    return unwrap(ZL_CCtx_compressMultiTypedRef(
            get(),
            output.data(),
            output.size(),
            inputPtrs.data(),
            inputPtrs.size()));
}

std::string CCtx::compress(poly::span<const Input> inputs)
{
    size_t inputSize = 0;
    for (auto const& input : inputs) {
        inputSize += input.contentSize();
        if (input.type() == Type::String) {
            inputSize += input.numElts() * sizeof(uint32_t);
        }
    }
    std::string output;
    output.resize(compressBound(inputSize), '\0');
    output.resize(compress(output, inputs));
    return output;
}

size_t CCtx::compressOne(poly::span<char> output, const Input& input)
{
    return compress(output, { &input, 1 });
}

std::string CCtx::compressOne(const Input& input)
{
    return compress({ &input, 1 });
}

size_t CCtx::compressSerial(poly::span<char> output, poly::string_view input)
{
    return compressOne(output, Input::refSerial(input));
}

std::string CCtx::compressSerial(poly::string_view input)
{
    return compressOne(Input::refSerial(input));
}

poly::string_view CCtx::getErrorContextString(ZL_Error error) const
{
    return ZL_CCtx_getErrorContextString_fromError(get(), error);
}

static void selectStartingGraphImpl(
        CCtx& cctx,
        const ZL_Compressor* compressor,
        GraphID graph,
        const poly::optional<GraphParameters>& params)
{
    ZL_RuntimeGraphParameters cParams{};
    if (params.has_value()) {
        cParams.name =
                params->name.has_value() ? params->name->c_str() : nullptr;
        if (params->customGraphs.has_value()) {
            cParams.customGraphs   = params->customGraphs->data();
            cParams.nbCustomGraphs = params->customGraphs->size();
        }
        if (params->customNodes.has_value()) {
            cParams.customNodes   = params->customNodes->data();
            cParams.nbCustomNodes = params->customNodes->size();
        }
        cParams.localParams = params->localParams.has_value()
                ? params->localParams->get()
                : nullptr;
    }
    cctx.unwrap(ZL_CCtx_selectStartingGraphID(
            cctx.get(),
            compressor,
            graph,
            params.has_value() ? &cParams : nullptr));
}

void CCtx::selectStartingGraph(
        GraphID graph,
        const poly::optional<GraphParameters>& params)
{
    selectStartingGraphImpl(*this, nullptr, graph, params);
}

void CCtx::selectStartingGraph(
        const Compressor& compressor,
        GraphID graph,
        const poly::optional<GraphParameters>& params)
{
    selectStartingGraphImpl(*this, compressor.get(), graph, params);
}

void CCtx::writeTraces(bool enabled, bool streamPreview)
{
    if ((bool)visHooks_ == enabled) {
        return; // no need to re-create or re-destroy the hooks
    }
    if (enabled) {
        visHooks_ = std::make_unique<visualizer::CompressionTraceHooks>(
                streamPreview);
        unwrap(ZL_CCtx_attachIntrospectionHooks(
                get(), visHooks_->getRawHooks()));
    } else {
        unwrap(ZL_CCtx_detachAllIntrospectionHooks(get()));
        visHooks_.reset();
    }
}

std::pair<
        poly::string_view,
        std::map<std::string, std::pair<poly::string_view, poly::string_view>>>
CCtx::getLatestTrace()
{
    if (!visHooks_) {
        throw Exception("Tracing is not enabled");
    }
    return (static_cast<visualizer::CompressionTraceHooks*>(visHooks_.get()))
            ->getLatestTrace();
}

} // namespace openzl
