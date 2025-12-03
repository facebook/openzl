// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <vector>

#include "openzl/cpp/CParam.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/CustomCodecDescription.hpp"
#include "openzl/cpp/Exception.hpp"
#include "openzl/cpp/Input.hpp"
#include "openzl/cpp/Output.hpp"
#include "openzl/cpp/poly/Optional.hpp"
#include "openzl/cpp/poly/Span.hpp"

namespace openzl {
class EncoderState {
   public:
    EncoderState(ZL_Encoder* encoder, poly::span<const ZL_Input*> inputs);

    EncoderState(const EncoderState&)            = delete;
    EncoderState& operator=(const EncoderState&) = delete;

    ZL_Encoder* get()
    {
        return encoder_;
    }
    const ZL_Encoder* get() const
    {
        return encoder_;
    }

    poly::span<const InputRef> inputs() const
    {
        return inputs_;
    }

    OutputRef createOutput(size_t idx, size_t maxNumElts, size_t eltWidth);

    int getCParam(CParam param) const;
    poly::optional<int> getLocalIntParam(int key) const;
    poly::optional<poly::span<const uint8_t>> getLocalParam(int key) const;

    void* getScratchSpace(size_t size);

    void sendCodecHeader(const void* header, size_t size);
    void sendCodecHeader(poly::span<const uint8_t> header)
    {
        return sendCodecHeader(header.data(), header.size());
    }
    void sendCodecHeader(poly::string_view header)
    {
        return sendCodecHeader(header.data(), header.size());
    }

   private:
    ZL_Encoder* encoder_;
    const std::vector<InputRef> inputs_;
};

class CustomEncoder {
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

    virtual poly::optional<LocalParams> localParams() const
    {
        return poly::nullopt;
    }

    virtual void encode(EncoderState& encoder) const = 0;
    virtual ~CustomEncoder()                         = default;

    static NodeID registerCustomEncoder(
            Compressor& compressor,
            std::shared_ptr<CustomEncoder> encoder);
};

} // namespace openzl
