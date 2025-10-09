// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/shared_components/clustering.h"
#include "custom_parsers/shared_components/numeric_graphs.h"
#include "custom_parsers/shared_components/string_graphs.h"
#include "openzl/compress/graphs/generic_clustering_graph.h"

ZL_GraphID ZL_createGraph_genericClustering(ZL_Compressor* compressor)
{
    static const size_t NUM_SUCCESSORS          = 9;
    const ZL_GraphID successors[NUM_SUCCESSORS] = {
        ZL_GRAPH_STORE,
        ZL_GRAPH_FIELD_LZ,
        ZL_GRAPH_ZSTD,
        ZL_GRAPH_COMPRESS_GENERIC,
        openzl::custom_parsers::ZL_Compressor_registerRangePack(compressor),
        openzl::custom_parsers::ZL_Compressor_registerRangePackZstd(compressor),
        openzl::custom_parsers::ZL_Compressor_registerTokenizeSorted(
                compressor),
        openzl::custom_parsers::ZL_Compressor_registerDeltaFieldLZ(compressor),
        openzl::custom_parsers::ZL_Compressor_registerStringTokenize(compressor)
    };

    ZL_ClusteringConfig config = {};

    std::array<ZL_NodeID, 4> clusteringCodecs = { ZL_NODE_CONCAT_SERIAL,
                                                  ZL_NODE_CONCAT_STRUCT,
                                                  ZL_NODE_CONCAT_NUMERIC,
                                                  ZL_NODE_CONCAT_STRING };

    return ZL_Clustering_registerGraphWithCustomClusteringCodecs(
            compressor,
            &config,
            successors,
            NUM_SUCCESSORS,
            clusteringCodecs.data(),
            clusteringCodecs.size());
}
