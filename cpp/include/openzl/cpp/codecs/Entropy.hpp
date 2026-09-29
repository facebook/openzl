// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/codecs/zl_entropy.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Graph.hpp"
#include "openzl/cpp/codecs/Metadata.hpp"

namespace openzl {
namespace graphs {

class Entropy : public SimpleGraph<Entropy> {
   public:
    static constexpr GraphID graph = ZL_GRAPH_ENTROPY;

    static constexpr GraphMetadata<1> metadata = {
        .inputs      = { InputMetadata{ .typeMask = TypeMask::Serial
                                           | TypeMask::Struct
                                           | TypeMask::Numeric } },
        .description = "Compress the input using an order-0 entropy compressor",
    };
};

class Huffman : public SimpleGraph<Huffman> {
   public:
    static constexpr GraphID graph = ZL_GRAPH_HUFFMAN;

    static constexpr GraphMetadata<1> metadata = {
        .inputs      = { InputMetadata{ .typeMask = TypeMask::Serial
                                           | TypeMask::Struct
                                           | TypeMask::Numeric } },
        .description = "Compress the input using Huffman",
    };
};

class Fse : public SimpleGraph<Fse> {
   public:
    static constexpr GraphID graph = ZL_GRAPH_FSE;

    static constexpr GraphMetadata<1> metadata = {
        .inputs      = { InputMetadata{ .typeMask = TypeMask::Serial } },
        .description = "Compress the input using FSE",
    };
};

class HuffmanPivCo : public SimpleGraph<HuffmanPivCo> {
   public:
    static constexpr GraphID graph = ZL_GRAPH_HUFFMAN_PIVCO;

    static constexpr GraphMetadata<1> metadata = {
        .inputs      = { InputMetadata{ .typeMask = TypeMask::Serial
                                           | TypeMask::Struct
                                           | TypeMask::Numeric } },
        .description = "Compress the input using PivCo Huffman",
    };
};

class HuffmanHuf0 : public SimpleGraph<HuffmanHuf0> {
   public:
    static constexpr GraphID graph = ZL_GRAPH_HUFFMAN_HUF0;

    static constexpr GraphMetadata<1> metadata = {
        .inputs      = { InputMetadata{ .typeMask = TypeMask::Serial
                                           | TypeMask::Struct
                                           | TypeMask::Numeric } },
        .description = "Compress the input using Huf0 Huffman",
    };
};

} // namespace graphs
} // namespace openzl
