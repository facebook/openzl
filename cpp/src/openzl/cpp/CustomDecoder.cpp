// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/CustomDecoder.hpp"

#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/Opaque.hpp"
#include "openzl/zl_dtransform.h"

namespace openzl {
DecoderState::DecoderState(
        ZL_Decoder* decoder,
        poly::span<const ZL_Input*> singletonInputs,
        poly::span<const ZL_Input*> variableInputs)
        : decoder_(decoder),
          singletonInputs_(
                  const_cast<ZL_Input**>(singletonInputs.data()),
                  const_cast<ZL_Input**>(
                          singletonInputs.data() + singletonInputs.size())),
          variableInputs_(
                  const_cast<ZL_Input**>(variableInputs.data()),
                  const_cast<ZL_Input**>(
                          variableInputs.data() + variableInputs.size()))
{
}

void* DecoderState::getScratchSpace(size_t size)
{
    auto scratch = ZL_Decoder_getScratchSpace(decoder_, size);
    if (scratch == nullptr) {
        throw ExceptionBuilder("DecoderState: Failed to get scratch space")
                .withErrorCode(ZL_ErrorCode_allocation);
    }
    return scratch;
}

OutputRef
DecoderState::createOutput(int index, size_t maxNumElts, size_t eltWidth)
{
    auto output =
            ZL_Decoder_createTypedStream(decoder_, index, maxNumElts, eltWidth);
    if (output == nullptr) {
        // TODO(terrelln): Query the most recent report from ZL_Decoder
        throw ExceptionBuilder("DecoderState: Failed to create output")
                .withErrorCode(ZL_ErrorCode_allocation);
    }
    return OutputRef(output);
}

poly::span<const uint8_t> DecoderState::getCodecHeader() const
{
    auto header = ZL_Decoder_getCodecHeader(decoder_);
    return { (const uint8_t*)header.start, header.size };
}

static ZL_Report decodeFn(
        ZL_Decoder* decoder,
        const ZL_Input* singletonInputs[],
        size_t numSingletonInputs,
        const ZL_Input* variableInputs[],
        size_t numVariableInputs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE(size_t, decoder);
    try {
        DecoderState state(
                decoder,
                { singletonInputs, numSingletonInputs },
                { variableInputs, numVariableInputs });
        const CustomDecoder* customDecoder =
                (const CustomDecoder*)ZL_Decoder_getOpaquePtr(decoder);
        customDecoder->decode(state);
    } catch (const Exception& e) {
        // TODO(terrelln): Better wrap the error
        ZL_ERR(GENERIC, "C++ openzl::Exception: %s", e.what());
    } catch (const std::exception& e) {
        ZL_ERR(GENERIC, "C++ std::exception: %s", e.what());
    } catch (...) {
        ZL_ERR(GENERIC, "C++ unknown exception");
    }
    return ZL_returnSuccess();
}

/* static */ void CustomDecoder::registerCustomDecoder(
        DCtx& dctx,
        std::shared_ptr<CustomDecoder> decoder)
{
    const auto& desc               = decoder->multiInputDescription();
    auto inputTypes                = typesToCTypes(desc.inputTypes);
    auto singletonOutputTypes      = typesToCTypes(desc.singletonOutputTypes);
    auto variableOutputTypes       = typesToCTypes(desc.variableOutputTypes);
    const ZL_MIGraphDesc graphDesc = {
        .CTid                = desc.id,
        .inputTypes          = inputTypes.data(),
        .nbInputs            = desc.inputTypes.size(),
        .lastInputIsVariable = desc.lastInputIsVariable,
        .soTypes             = singletonOutputTypes.data(),
        .nbSOs               = desc.singletonOutputTypes.size(),
        .voTypes             = variableOutputTypes.data(),
        .nbVOs               = desc.variableOutputTypes.size(),
    };
    const ZL_MIDecoderDesc decoderDesc = {
        .gd          = graphDesc,
        .transform_f = decodeFn,
        .name        = desc.name.has_value() ? desc.name->c_str() : nullptr,
        .opaque      = moveToOpaquePtr(std::move(decoder)),
    };
    dctx.registerCustomDecoder(decoderDesc);
}

} // namespace openzl
