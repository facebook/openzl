// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <openzl/openzl.hpp>

namespace openzl::lz {

class VarByteEncoder : public CustomEncoder {
   public:
    SimpleCodecDescription simpleCodecDescription() const override;

    void encode(EncoderState& encoder) const override;

    ~VarByteEncoder() override = default;
};

class VarByteDecoder : public CustomDecoder {
   public:
    SimpleCodecDescription simpleCodecDescription() const override;

    void decode(DecoderState& decoder) const override;

    ~VarByteDecoder() override = default;
};

} // namespace openzl::lz
