// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <openzl/openzl.hpp>

namespace openzl::lz {

class IsoByteEncoder : public CustomEncoder {
   public:
    VariableOutputCodecDescription variableOutputDescription() const override;

    void encode(EncoderState& encoder) const override;

    ~IsoByteEncoder() override = default;
};

class IsoByteDecoder : public CustomDecoder {
   public:
    VariableOutputCodecDescription variableOutputDescription() const override;

    void decode(DecoderState& decoder) const override;

    ~IsoByteDecoder() override = default;
};

} // namespace openzl::lz
