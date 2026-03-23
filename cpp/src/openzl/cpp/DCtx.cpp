// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/DCtx.hpp"

#include "openzl/cpp/CustomDecoder.hpp"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/FrameInfo.hpp"
#include "openzl/cpp/Output.hpp"
#include "openzl/cpp/experimental/trace/DecompressionTraceHooks.hpp"
#include "openzl/zl_decompress.h"

namespace openzl {
DCtx::DCtx() : DCtx(ZL_DCtx_create(), ZL_DCtx_free) {}
DCtx::DCtx(DCtx&&) noexcept            = default;
DCtx& DCtx::operator=(DCtx&&) noexcept = default;
DCtx::~DCtx()                          = default;

void DCtx::setParameter(DParam param, int value)
{
    unwrap(ZL_DCtx_setParameter(get(), static_cast<ZL_DParam>(param), value));
}

int DCtx::getParameter(DParam param) const
{
    return ZL_DCtx_getParameter(get(), static_cast<ZL_DParam>(param));
}

void DCtx::resetParameters()
{
    unwrap(ZL_DCtx_resetParameters(get()));
}

void DCtx::decompress(poly::span<Output> outputs, poly::string_view input)
{
    if (outputs.size() == 1) {
        unwrap(ZL_DCtx_decompressTBuffer(
                get(), outputs[0].get(), input.data(), input.size()));
    } else {
        std::vector<ZL_Output*> outputPtrs;
        outputPtrs.reserve(outputs.size());
        for (auto& output : outputs) {
            outputPtrs.push_back(output.get());
        }
        unwrap(ZL_DCtx_decompressMultiTBuffer(
                get(),
                outputPtrs.data(),
                outputPtrs.size(),
                input.data(),
                input.size()));
    }
}

std::vector<Output> DCtx::decompress(poly::string_view input)
{
    FrameInfo info(input);
    std::vector<Output> outputs(info.numOutputs());
    decompress(outputs, input);
    return outputs;
}

void DCtx::decompressOne(Output& output, poly::string_view input)
{
    decompress({ &output, 1 }, input);
}

Output DCtx::decompressOne(poly::string_view input)
{
    Output out;
    decompressOne(out, input);
    return out;
}

size_t DCtx::decompressSerial(poly::span<char> output, poly::string_view input)
{
    auto out = Output::wrapSerial(output);
    decompressOne(out, input);
    return out.contentSize();
}

std::string DCtx::decompressSerial(poly::string_view input)
{
    std::string out;
    FrameInfo info(input);
    out.resize(info.outputContentSize(0), '\0');
    out.resize(decompressSerial(out, input));
    return out;
}

void DCtx::registerCustomDecoder(const ZL_MIDecoderDesc& desc)
{
    unwrap(ZL_DCtx_registerMIDecoder(get(), &desc));
}

void DCtx::registerCustomDecoder(std::shared_ptr<CustomDecoder> decoder)
{
    CustomDecoder::registerCustomDecoder(*this, std::move(decoder));
}

poly::string_view DCtx::getErrorContextString(ZL_Error error) const
{
    return ZL_DCtx_getErrorContextString_fromError(get(), error);
}

void DCtx::writeTraces(bool enabled)
{
    if ((bool)visHooks_ == enabled) {
        return; // no need to re-create or re-destroy the hooks
    }
    if (enabled) {
        visHooks_ = std::make_unique<visualizer::DecompressionTraceHooks>();
        unwrap(ZL_DCtx_attachDecompressIntrospectionHooks(
                get(), visHooks_->getRawHooks()));
    } else {
        unwrap(ZL_DCtx_detachAllDecompressIntrospectionHooks(get()));
        visHooks_.reset();
    }
}

std::pair<
        poly::string_view,
        std::map<std::string, std::pair<poly::string_view, poly::string_view>>>
DCtx::getLatestTrace()
{
    if (!visHooks_) {
        throw Exception("Tracing is not enabled");
    }
    return (static_cast<visualizer::DecompressionTraceHooks*>(visHooks_.get()))
            ->getLatestTrace();
}

} // namespace openzl
