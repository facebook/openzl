// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <openzl/openzl.hpp>

namespace openzl::lz {

class BucketEncoder : public CustomEncoder {
   public:
    SimpleCodecDescription simpleCodecDescription() const override;

    void encode(EncoderState& encoder) const override;

    static Edge::RunNodeResult runNode(
            Edge& edge,
            NodeID node,
            poly::span<const uint8_t> bases,
            uint8_t maxSymbolValue);

    ~BucketEncoder() override = default;
};

class BucketDecoder : public CustomDecoder {
   public:
    SimpleCodecDescription simpleCodecDescription() const override;

    void decode(DecoderState& decoder) const override;

    ~BucketDecoder() override = default;
};

class BucketGraph : public FunctionGraph {
   public:
    BucketGraph() = default;

    static GraphID create(Compressor& compressor, bool entropyCompressFixed);

    FunctionGraphDescription functionGraphDescription() const override;

    void graph(GraphState& state) const override;

    virtual ~BucketGraph() override = default;
};

} // namespace openzl::lz
