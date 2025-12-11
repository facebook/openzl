// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>

#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"

#include "tests/datagen/structures/FixedWidthDataProducer.h"

namespace openzl::tests::datagen::test_registry {

// NOTE: The IDs must remain stable!
enum class TransformID : int {
    ThriftKernelMapI32Float         = 1,
    ThriftKernelMapI32ArrayFloat    = 2,
    ThriftKernelMapI32ArrayI64      = 3,
    ThriftKernelMapI32ArrayArrayI64 = 4,
    ThriftKernelMapI32MapI64Float   = 5,
    ThriftKernelArrayI64            = 6,
    ThriftKernelArrayI32            = 7,
    ThriftKernelArrayFloat          = 8,
    TulipV2                         = 9,
    SplitByStruct                   = 10,
    SplitN                          = 11,
    DispatchNByTag                  = 12,
    Bitunpack7                      = 13,
    Bitunpack64                     = 14,
    ThriftCompact                   = 15,
    ThriftBinary                    = 16,
    TransposeSplit                  = 17,
    FieldLz                         = 18,
    CustomTokenize                  = 19,
    ThriftCompactPrevFormatVersion  = 20,
    ThriftBinaryPrevFormatVersion   = 21,
    ThriftCompactMaxFormatVersion   = 22,
    ThriftBinaryMaxFormatVersion    = 23,
    SplitNStruct                    = 24,
    SplitNNumeric                   = 25,
    FSENCount                       = 26,
};

struct CustomNode {
    std::function<ZL_NodeID(ZL_Compressor*)> registerEncoder;
    std::optional<std::function<void(ZL_DCtx*)>> registerDecoder;
    std::unique_ptr<tests::datagen::FixedWidthDataProducer> dataProducer;
};

struct CustomGraph {
    std::function<ZL_GraphID(ZL_Compressor*)> registerEncoder;
    std::optional<std::function<void(ZL_DCtx*)>> registerDecoder;
    std::unique_ptr<tests::datagen::FixedWidthDataProducer> dataProducer;
};

std::unordered_map<TransformID, CustomNode> const& getCustomNodes();

std::unordered_map<TransformID, CustomGraph> const& getCustomGraphs();
} // namespace openzl::tests::datagen::test_registry
