// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cli/utils/profile_graphs.h"

#include <limits>

#include "custom_parsers/csv/csv_profile.h"
#include "custom_parsers/parquet/parquet_graph.h"
#include "custom_parsers/pytorch_model_parser.h"
#include "custom_parsers/shared_components/clustering.h"
#include "openzl/codecs/zl_ace.h"
#include "openzl/codecs/zl_generic.h"
#include "openzl/codecs/zl_illegal.h"
#include "openzl/codecs/zl_lz.h"
#include "openzl/codecs/zl_segmenters.h"
#include "openzl/codecs/zl_zstd.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/codecs/Lz.hpp"
#include "openzl/zl_compressor.h"
#include "openzl/zl_errors.h"

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

ZL_GraphID buildLzGraph(ZL_Compressor* compressor, size_t chunkByteSize)
{
    CompressorRef c(compressor);
    ZL_GraphID inner = graphs::Lz{}(c);
    return ZL_Compressor_buildSerialSegmenter(compressor, chunkByteSize, inner);
}

ZL_GraphID buildZstdGraph(ZL_Compressor* compressor)
{
    auto result = ZL_Compressor_buildTrainableZstdGraph(compressor);
    if (ZL_RES_isError(result)) {
        return ZL_GRAPH_ILLEGAL;
    }
    return ZL_RES_value(result);
}

ZL_GraphID
buildCsvGraph(ZL_Compressor* compressor, size_t chunkByteSize, char separator)
{
    char sep = ',';
    if (separator) {
        sep = separator;
    }
    if (chunkByteSize == 0) {
        chunkByteSize = custom_parsers::kDefaultChunkSize;
    }
    return custom_parsers::ZL_createGraph_genericCSVCompressorWithOptions(
            compressor, chunkByteSize, true, sep, false);
}

ZL_GraphID buildParquetGraph(
        ZL_Compressor* compressor,
        size_t chunkByteSize = custom_parsers::kDefaultChunkSize)
{
    if (chunkByteSize > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return ZL_GRAPH_ILLEGAL;
    }
    ZL_GraphID clustering = ZS2_createGraph_genericClustering(compressor);
    if (!ZL_GraphID_isValid(clustering)) {
        return ZL_GRAPH_ILLEGAL;
    }
    return ZL_Parquet_registerGraph_withChunkSize(
            compressor, clustering, static_cast<int>(chunkByteSize));
}

ZL_GraphID buildPytorchGraph(ZL_Compressor* compressor)
{
    return ZS2_createGraph_pytorchModelCompressor(compressor);
}

} // namespace openzl::profiles
