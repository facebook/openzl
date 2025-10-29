// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/protobuf/ProtoGraph.h"
#include "openzl/compress/graphs/generic_clustering_graph.h"
#include "openzl/zl_dyngraph.h"

namespace openzl {
namespace protobuf {
namespace {
ZL_GraphID createClusteringGraph(ZL_Compressor* compressor, ZL_GraphID ace)
{
    static const size_t NUM_SUCCESSORS          = 1;
    const ZL_GraphID successors[NUM_SUCCESSORS] = {
        ace,
    };

    ZL_ClusteringConfig config = {};

    return ZL_Clustering_registerGraph(
            compressor, &config, successors, NUM_SUCCESSORS);
}
} // namespace
ZL_GraphID ZL_Protobuf_registerGraph(ZL_Compressor* compressor)
{
    auto ace = ZL_Compressor_buildACEGraph(compressor);
    return createClusteringGraph(compressor, ace);
}
} // namespace protobuf
} // namespace openzl
