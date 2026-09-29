// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/zl_ctransform.h"
#include "openzl/zl_dtransform.h"

#include <string_view>

namespace zstrong::thrift {

// TODO(T193417384) Currently this header exposes functions to register the
// Thrift node, which means Managed Compression and unit tests must separately
// build the Thrift graph. It would be much cleaner to build the graph in
// thrift_parsers.cpp and simply take successor graphs as arguments (dependency
// injection). This would mean deprecating most of the symbols in this header.

constexpr int kMinFormatVersionEncode         = 10;
constexpr int kMinFormatVersionDecode         = 10;
constexpr int kMinFormatVersionEncodeTulipV2  = 12;
constexpr int kMinFormatVersionEncodeClusters = 12;
constexpr int kMinFormatVersionStringVSF      = 14;

enum ThriftTransformIDs : ZL_IDType {
    kThriftCompactConfigurable = 1002,
    kThriftBinaryConfigurable  = 1003,
};

extern ZL_VOEncoderDesc const thriftCompactConfigurableSplitter;
extern ZL_VODecoderDesc const thriftCompactConfigurableUnSplitter;

extern ZL_VOEncoderDesc const thriftBinaryConfigurableSplitter;
extern ZL_VODecoderDesc const thriftBinaryConfigurableUnSplitter;

/**
 * The configurable Thrift encoder/decoder implementation functions.
 */
ZL_Report configurableEncodeCompact(
        ZL_Encoder* eictx,
        const ZL_Input* in) noexcept;
ZL_Report configurableEncodeBinary(
        ZL_Encoder* eictx,
        const ZL_Input* in) noexcept;
ZL_Report configurableDecodeCompact(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs) noexcept;
ZL_Report configurableDecodeBinary(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs) noexcept;

ZL_Report registerCustomTransforms(ZL_DCtx* dctx);

ZL_NodeID registerCompactTransform(ZL_Compressor* cgraph, ZL_IDType id);
ZL_Report registerCompactTransform(ZL_DCtx* dctx, ZL_IDType id);
ZL_NodeID registerBinaryTransform(ZL_Compressor* cgraph, ZL_IDType id);
ZL_Report registerBinaryTransform(ZL_DCtx* dctx, ZL_IDType id);

// Works for both Binary and Compact nodes
ZL_NodeID cloneThriftNodeWithLocalParams(
        ZL_Compressor* cgraph,
        ZL_NodeID nodeId,
        std::string_view serializedConfig);

} // namespace zstrong::thrift
