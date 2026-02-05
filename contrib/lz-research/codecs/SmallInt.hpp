// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <openzl/openzl.hpp>

namespace openzl::lz {

class SmallIntEncoder : public CustomEncoder {
   public:
    SimpleCodecDescription simpleCodecDescription() const override;

    void encode(EncoderState& encoder) const override;

    ~SmallIntEncoder() override = default;
};

class SmallIntDecoder : public CustomDecoder {
   public:
    SimpleCodecDescription simpleCodecDescription() const override;

    void decode(DecoderState& decoder) const override;

    ~SmallIntDecoder() override = default;
};

} // namespace openzl::lz
