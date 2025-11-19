// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_PARSERS_PARQUET_GRAPH_H
#define CUSTOM_PARSERS_PARQUET_GRAPH_H

#include "openzl/shared/portability.h"
#include "openzl/zl_compressor.h"

ZL_BEGIN_C_DECLS

/**
 * Registration function for the Parquet graph.
 *
 * @param compressor The compressor to register the graph with.
 * @param clusteringGraph The clustering graph to use as a successor.
 *
 * @warning This graph will fail to compress if the input is not a valid Parquet
 * file in the canonical format. You can produve a canonical Parquet file using
 * the canonicalization script (/tools/parquet/make_canonical_parquet.cpp).
 */
ZL_GraphID ZL_Parquet_registerGraph(
        ZL_Compressor* compressor,
        ZL_GraphID clusteringGraph);

/**
 * Registration function for the Parquet graph.
 *
 * @param compressor The compressor to register the graph with.
 * @param clusteringGraph The clustering graph to use as a successor.
 * @param chunkSize The size of the chunks to split the input into. The
 * default is no chunking (set to 0).
 */
ZL_GraphID ZL_Parquet_registerGraph_withChunkSize(
        ZL_Compressor* compressor,
        ZL_GraphID clusteringGraph,
        int chunkSize);

ZL_END_C_DECLS

#endif
