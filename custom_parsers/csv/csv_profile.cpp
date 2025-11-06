// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/csv/csv_profile.h"
#include "custom_parsers/csv/csv_segmenter.h"
#include "custom_parsers/shared_components/numeric_graphs.h"
#include "custom_parsers/shared_components/string_graphs.h"
#include "openzl/codecs/zl_clustering.h"
#include "openzl/codecs/zl_parse_int.h"

namespace openzl::custom_parsers {

static const size_t NUM_DEFAULT_TYPES = 1;

static const ZL_Type defaultSuccessorTypes[NUM_DEFAULT_TYPES] = {
    ZL_Type_string // String (variable length)
};

static const size_t defaultSuccessorIdxs[NUM_DEFAULT_TYPES] = { 1 };

static const size_t defaultSuccessorEltWidths[NUM_DEFAULT_TYPES] = { 0 };

static const size_t defaultClusteringCodecIdxs[NUM_DEFAULT_TYPES] = { 3 };

ZL_GraphID ZL_createGraph_genericCSVCompressor(
        ZL_Compressor* compressor) noexcept
{
    return ZL_createGraph_genericCSVCompressorWithOptions(
            compressor, kDefaultChunkSize, true, ',', false);
}

ZL_GraphID ZL_createGraph_genericCSVCompressorWithOptions(
        ZL_Compressor* compressor,
        size_t chunkByteSizeMax,
        bool hasHeader,
        char separator,
        bool useNullAware) noexcept
{
    const auto parseExceptionsGraph = ZL_GRAPH_COMPRESS_GENERIC;
    const auto flz1 =
            ZL_Compressor_registerFieldLZGraph_withLevel(compressor, 1);
    const auto flz = ZL_RES_value(ZL_Compressor_parameterizeTryParseIntGraph(
            compressor, flz1, parseExceptionsGraph));

    std::array<ZL_GraphID, 3> hpCustomGraphs = {
        ZL_GRAPH_ENTROPY,
        ZL_GRAPH_COMPRESS_GENERIC,
        flz,
    };

    // TODO: pass the codecs from the csv profile directly into training
    std::array<ZL_NodeID, 4> clusteringCodecs = { ZL_NODE_CONCAT_SERIAL,
                                                  ZL_NODE_CONCAT_STRUCT,
                                                  ZL_NODE_CONCAT_NUMERIC,
                                                  ZL_NODE_INTERLEAVE_STRING };

    // null-aware dispatch
    const auto nullAwareFlz = openzl::custom_parsers::registerNullAwareDispatch(
            compressor, "nullAwareFlz", hpCustomGraphs);

    const auto numericTokenize =
            ZL_RES_value(ZL_Compressor_parameterizeTryParseIntGraph(
                    compressor,
                    openzl::custom_parsers::
                            ZL_Compressor_registerTokenizeSorted(compressor),
                    parseExceptionsGraph));
    hpCustomGraphs[2] = numericTokenize;
    const auto nullAwareNumericTokenize =
            openzl::custom_parsers::registerNullAwareDispatch(
                    compressor, "nullAwareNumericTokenize", hpCustomGraphs);
    const auto stringTokenize =
            openzl::custom_parsers::ZL_Compressor_registerStringTokenize(
                    compressor);

    std::array<ZL_GraphID, 5> successors = { ZL_GRAPH_STORE,
                                             ZL_GRAPH_COMPRESS_GENERIC,
                                             nullAwareFlz,
                                             nullAwareNumericTokenize,
                                             stringTokenize };

    // Create type defaults for each type
    ZL_ClusteringConfig_TypeSuccessor typeDefaults[NUM_DEFAULT_TYPES];
    for (size_t i = 0; i < NUM_DEFAULT_TYPES; i++) {
        typeDefaults[i].type               = defaultSuccessorTypes[i];
        typeDefaults[i].eltWidth           = defaultSuccessorEltWidths[i];
        typeDefaults[i].successorIdx       = defaultSuccessorIdxs[i];
        typeDefaults[i].clusteringCodecIdx = defaultClusteringCodecIdxs[i];
    }

    // Create default clusters for each type
    ZL_ClusteringConfig_Cluster defaultClusters[NUM_DEFAULT_TYPES];
    for (size_t i = 0; i < NUM_DEFAULT_TYPES; i++) {
        defaultClusters[i].typeSuccessor.type = defaultSuccessorTypes[i];
        defaultClusters[i].typeSuccessor.eltWidth =
                defaultSuccessorEltWidths[i];
        defaultClusters[i].typeSuccessor.successorIdx = defaultSuccessorIdxs[i];
        defaultClusters[i].typeSuccessor.clusteringCodecIdx =
                defaultClusteringCodecIdxs[i];
        defaultClusters[i].memberTags =
                nullptr; // No specific member tags for default clusters
        defaultClusters[i].nbMemberTags = 0;
    }

    // Configure with both type defaults and default clusters
    ZL_ClusteringConfig config = { .clusters       = defaultClusters,
                                   .nbClusters     = NUM_DEFAULT_TYPES,
                                   .typeDefaults   = typeDefaults,
                                   .nbTypeDefaults = NUM_DEFAULT_TYPES };

    ZL_GraphID clusteringGraph =
            ZL_Clustering_registerGraphWithCustomClusteringCodecs(
                    compressor,
                    &config,
                    successors.data(),
                    successors.size(),
                    clusteringCodecs.data(),
                    clusteringCodecs.size());

    // TODO support non-comma separators
    return ZL_RES_value(ZL_CsvSegmenter_registerSegmenter(
            compressor,
            chunkByteSizeMax,
            hasHeader,
            separator,
            useNullAware,
            clusteringGraph));
}

} // namespace openzl::custom_parsers
