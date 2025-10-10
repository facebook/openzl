// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/cpp/CustomEncoder.hpp"

#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/Opaque.hpp"
#include "openzl/zl_ctransform.h"

namespace openzl {

EncoderState::EncoderState(
        ZL_Encoder* encoder,
        poly::span<const ZL_Input*> inputs)
        : encoder_(encoder),
          inputs_(const_cast<ZL_Input**>(inputs.data()),
                  const_cast<ZL_Input**>(inputs.data()) + inputs.size())
{
}

OutputRef
EncoderState::createOutput(size_t idx, size_t maxNumElts, size_t eltWidth)
{
    auto output = ZL_Encoder_createTypedStream(
            encoder_, int(idx), maxNumElts, eltWidth);
    if (output == nullptr) {
        // TODO(terrelln): Query the most recent report from ZL_Encoder
        throw ExceptionBuilder("EncoderState: Failed to create output")
                .withErrorCode(ZL_ErrorCode_allocation);
    }
    return OutputRef(output);
}

int EncoderState::getCParam(CParam param) const
{
    return ZL_Encoder_getCParam(encoder_, ZL_CParam(int(param)));
}

poly::optional<int> EncoderState::getLocalIntParam(int key) const
{
    auto param = ZL_Encoder_getLocalIntParam(encoder_, key);
    if (param.paramId == ZL_LP_INVALID_PARAMID) {
        return poly::nullopt;
    }
    return param.paramValue;
}

poly::optional<poly::span<const uint8_t>> EncoderState::getLocalParam(
        int key) const
{
    auto param = ZL_Encoder_getLocalParam(encoder_, key);
    if (param.paramId == ZL_LP_INVALID_PARAMID) {
        return poly::nullopt;
    }
    return poly::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(param.paramRef), param.paramSize);
}

void* EncoderState::getScratchSpace(size_t size)
{
    auto scratch = ZL_Encoder_getScratchSpace(encoder_, size);
    if (scratch == nullptr) {
        throw ExceptionBuilder("EncoderState: Failed to get scratch space")
                .withErrorCode(ZL_ErrorCode_allocation);
    }
    return scratch;
}

void EncoderState::sendCodecHeader(const void* header, size_t size)
{
    // TODO(terrelln): Make this return a ZL_Report.
    ZL_Encoder_sendCodecHeader(encoder_, header, size);
}

static ZL_Report encodeFn(
        ZL_Encoder* encoder,
        const ZL_Input* inputs[],
        size_t numInputs) noexcept
{
    ZL_RESULT_DECLARE_SCOPE(size_t, encoder);
    try {
        EncoderState state(encoder, { inputs, numInputs });
        const CustomEncoder* customEncoder =
                (const CustomEncoder*)ZL_Encoder_getOpaquePtr(encoder);
        customEncoder->encode(state);
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

/* static */ NodeID CustomEncoder::registerCustomEncoder(
        Compressor& compressor,
        std::shared_ptr<CustomEncoder> encoder)
{
    const auto& desc               = encoder->multiInputDescription();
    auto inputTypes                = typesToCTypes(desc.inputTypes);
    auto singletonOutputTypes      = typesToCTypes(desc.singletonOutputTypes);
    auto variableOutputTypes       = typesToCTypes(desc.variableOutputTypes);
    auto localParams               = encoder->localParams();
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
    ZL_MIEncoderDesc encoderDesc = {
        .gd          = graphDesc,
        .transform_f = encodeFn,
        .name        = desc.name.has_value() ? desc.name->c_str() : nullptr,
        .opaque      = moveToOpaquePtr(std::move(encoder)),
    };
    if (localParams.has_value()) {
        encoderDesc.localParams = **localParams;
    }
    return compressor.registerCustomEncoder(encoderDesc);
};

} // namespace openzl
