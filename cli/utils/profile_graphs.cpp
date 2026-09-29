// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cli/utils/profile_graphs.h"

#include "openzl/codecs/zl_ace.h"
#include "openzl/codecs/zl_generic.h"
#include "openzl/codecs/zl_illegal.h"
#include "openzl/codecs/zl_lz.h"
#include "openzl/codecs/zl_segmenters.h"
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
        ZL_NodeID conversionNode,
        size_t chunkByteSize)
{
    ZL_GraphID graph = ZL_Compressor_buildACEGraphWithDefault(
            compressor, ZL_GRAPH_NUMERIC);
    if (!ZL_GraphID_isValid(graph)) {
        return ZL_GRAPH_ILLEGAL;
    }
    graph = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor, conversionNode, graph);
    if (!ZL_GraphID_isValid(graph)) {
        return ZL_GRAPH_ILLEGAL;
    }
    return ZL_Compressor_buildNumFromSerialSegmenter(
            compressor, eltByteWidth, chunkByteSize, graph);
}

} // namespace openzl::profiles
