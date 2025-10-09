// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CUSTOM_PARSERS_SHARED_COMPONENTS_CLUSTERING_H
#define OPENZL_CUSTOM_PARSERS_SHARED_COMPONENTS_CLUSTERING_H

#include "openzl/zl_compressor.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @brief Registers a generic clustering graph where clustering is still
 * unconfigured.
 *
 * @param compressor The compressor to register the graph with
 * @returns The graph ID registered for the clustering graph
 */
ZL_GraphID ZL_createGraph_genericClustering(ZL_Compressor* compressor);

#if defined(__cplusplus)
}
#endif

#endif // ZSTRONG_CUSTOM_PARSERS_SHARED_COMPONENTS_CLUSTERING_H
