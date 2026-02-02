// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <openzl/openzl.hpp>

namespace openzl::lz {

class Bucket16Encoder : public CustomEncoder {
   public:
    SimpleCodecDescription simpleCodecDescription() const override;

    void encode(EncoderState& encoder) const override;

    static Edge::RunNodeResult runNode(
            Edge& edge,
            NodeID node,
            poly::span<const uint16_t> bases,
            uint16_t maxSymbolValue);

    ~Bucket16Encoder() override = default;
};

class Bucket16Decoder : public CustomDecoder {
   public:
    SimpleCodecDescription simpleCodecDescription() const override;

    void decode(DecoderState& decoder) const override;

    ~Bucket16Decoder() override = default;
};

class Bucket16Graph : public FunctionGraph {
   public:
    Bucket16Graph() = default;

    static GraphID create(Compressor& compressor);

    FunctionGraphDescription functionGraphDescription() const override;

    void graph(GraphState& state) const override;

    virtual ~Bucket16Graph() override = default;
};

} // namespace openzl::lz
