// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cli/utils/profile_graphs.h"

#include "openzl/codecs/zl_ace.h"
#include "openzl/codecs/zl_conversion.h"
#include "openzl/codecs/zl_field_lz.h"
#include "openzl/codecs/zl_illegal.h"
#include "openzl/codecs/zl_lz.h"
#include "openzl/codecs/zl_segmenters.h"
#include "openzl/codecs/zl_zigzag.h"
#include "openzl/zl_compressor.h"

namespace openzl::profiles {

ZL_GraphID buildSerialGraph(ZL_Compressor* compressor, size_t chunkByteSize)
{
    ZL_GraphID inner =
            ZL_Compressor_buildACEGraphWithDefault(compressor, ZL_GRAPH_LZ);
    if (!ZL_GraphID_isValid(inner)) {
        return ZL_GRAPH_ILLEGAL;
    }
    return ZL_Compressor_buildSerialSegmenter(compressor, chunkByteSize, inner);
}

ZL_GraphID buildIntGraph(
        ZL_Compressor* compressor,
        size_t eltByteWidth,
        bool isSigned,
        size_t chunkByteSize)
{
    const size_t bitWidth = eltByteWidth * 8;

    ZL_GraphID graph = ZL_GRAPH_FIELD_LZ;
    if (isSigned) {
        graph = ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor, ZL_NODE_ZIGZAG, graph);
        if (!ZL_GraphID_isValid(graph)) {
            return ZL_GRAPH_ILLEGAL;
        }
    }
    graph = ZL_Compressor_buildACEGraphWithDefault(compressor, graph);
    if (!ZL_GraphID_isValid(graph)) {
        return ZL_GRAPH_ILLEGAL;
    }
    graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor, ZL_Node_interpretAsLE(bitWidth), graph);
    if (!ZL_GraphID_isValid(graph)) {
        return ZL_GRAPH_ILLEGAL;
    }
    return ZL_Compressor_buildNumFromSerialSegmenter(
            compressor, eltByteWidth, chunkByteSize, graph);
}

} // namespace openzl::profiles
