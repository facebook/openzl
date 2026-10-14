// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "cli/utils/profile_graphs.h"

#include <limits>
#include <map>

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

const std::string& getProfileDescription(const std::string& name)
{
    static const std::map<std::string, std::string> kDescriptions = {
        { "be-i16", "Big-endian signed 16-bit data" },
        { "be-i32", "Big-endian signed 32-bit data" },
        { "be-i64", "Big-endian signed 64-bit data" },
        { "be-u16", "Big-endian unsigned 16-bit data" },
        { "be-u32", "Big-endian unsigned 32-bit data" },
        { "be-u64", "Big-endian unsigned 64-bit data" },
        { "csv",
          "CSV. Pass optional non-comma separator with --profile-arg <char>." },
        { "i8", "Signed 8-bit data" },
        { "le-i16", "Little-endian signed 16-bit data" },
        { "le-i32", "Little-endian signed 32-bit data" },
        { "le-i64", "Little-endian signed 64-bit data" },
        { "le-u16", "Little-endian unsigned 16-bit data" },
        { "le-u32", "Little-endian unsigned 32-bit data" },
        { "le-u64", "Little-endian unsigned 64-bit data" },
        { "lz", "Trainable LZ compressor" },
        { "numeric-ml-selector-64",
          "64 bit numeric data using ml selectors (Placeholder)" },
        { "parquet",
          "Parquet in the canonical format (no compression, plain encoding)" },
        { "pytorch",
          "Pytorch model generated from torch.save(). Training is not supported." },
        { "sao", "SAO format from the Silesia corpus" },
        { "sddl",
          "Data that can be parsed using the Simple Data Description Language. Pass a path to the data description file with --profile-arg." },
        { "sddl2",
          "Data that can be parsed using Simple Data Description Language v2 (https://openzl.org/sddl/). Pass a path to the description file with --profile-arg." },
        { "serial", "Serial data (aka raw bytes)" },
        { "u8", "Unsigned 8-bit data" },
        { "zstd", "Use this profile to train a Zstd dict" },
    };
    return kDescriptions.at(name);
}

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

ZL_GraphID buildParquetGraph(ZL_Compressor* compressor, size_t chunkByteSize)
{
    if (chunkByteSize == 0) {
        chunkByteSize = custom_parsers::kDefaultChunkSize;
    }
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
