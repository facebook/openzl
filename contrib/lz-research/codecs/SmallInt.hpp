// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <openzl/openzl.hpp>

namespace openzl::lz {

struct SmallInt {
    static size_t
    encode(uint8_t* small, uint16_t* large, const uint16_t* src, size_t n);
    static void decode(
            uint16_t* dst,
            const uint8_t* small,
            size_t numSmall,
            const uint16_t* large,
            size_t numLarge);
};

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
