// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/shared_components/numeric_graphs.h"

#include "openzl/zl_stream.h"

#include "openzl/zl_selector_declare_helper.h"

namespace openzl::custom_parsers {

ZL_GraphID ZL_Compressor_registerRangePack(ZL_Compressor* compressor)
{
    const ZL_GraphID rangePackFieldLZ =
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    compressor, ZL_NODE_RANGE_PACK, ZL_GRAPH_FIELD_LZ);
    return rangePackFieldLZ;
}

ZL_GraphID ZL_Compressor_registerRangePackZstd(ZL_Compressor* compressor)
{
    const ZL_GraphID rangePackZstd =
            ZL_Compressor_registerStaticGraph_fromNode1o(
                    compressor, ZL_NODE_RANGE_PACK, ZL_GRAPH_ZSTD);
    return rangePackZstd;
}

ZL_GraphID ZL_Compressor_registerTokenizeSorted(ZL_Compressor* compressor)
{
    auto const delta = ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor, ZL_NODE_DELTA_INT, ZL_GRAPH_FIELD_LZ);
    return ZL_Compressor_registerTokenizeGraph(
            compressor, ZL_Type_numeric, true, delta, ZL_GRAPH_FIELD_LZ);
}

ZL_GraphID ZL_Compressor_registerDeltaFieldLZ(ZL_Compressor* compressor)
{
    return ZL_Compressor_registerStaticGraph_fromNode1o(
            compressor, ZL_NODE_DELTA_INT, ZL_GRAPH_FIELD_LZ);
}
} // namespace openzl::custom_parsers
