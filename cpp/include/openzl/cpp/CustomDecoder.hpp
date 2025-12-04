// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <vector>

#include "openzl/cpp/CustomCodecDescription.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/Input.hpp"
#include "openzl/cpp/Output.hpp"
#include "openzl/cpp/poly/Span.hpp"

namespace openzl {
class DecoderState {
   public:
    DecoderState(
            ZL_Decoder* decoder,
            poly::span<const ZL_Input*> singletonInputs,
            poly::span<const ZL_Input*> variableInputs);

    DecoderState(const DecoderState&)            = delete;
    DecoderState& operator=(const DecoderState&) = delete;

    poly::span<const InputRef> singletonInputs() const
    {
        return singletonInputs_;
    }

    poly::span<const InputRef> variableInputs() const
    {
        return variableInputs_;
    }

    void* getScratchSpace(size_t size);
    OutputRef createOutput(int index, size_t maxNumElts, size_t eltWidth);
    poly::span<const uint8_t> getCodecHeader() const;

   private:
    ZL_Decoder* decoder_;
    const std::vector<InputRef> singletonInputs_;
    const std::vector<InputRef> variableInputs_;
};

class CustomDecoder {
   public:
    virtual MultiInputCodecDescription multiInputDescription() const
    {
        return MultiInputCodecDescription::fromVariableOutput(
                variableOutputDescription());
    }
    virtual VariableOutputCodecDescription variableOutputDescription() const
    {
        return VariableOutputCodecDescription::fromSimple(
                simpleCodecDescription());
    }
    virtual SimpleCodecDescription simpleCodecDescription() const
    {
        throw Exception("Not implemented");
    }

    virtual void decode(DecoderState& decoder) const = 0;
    virtual ~CustomDecoder()                         = default;

    static void registerCustomDecoder(
            DCtx& dctx,
            std::shared_ptr<CustomDecoder> decoder);
};

} // namespace openzl
