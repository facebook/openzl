// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_GENERIC_CLUSTERING_GRAPH_H
#define ZSTRONG_GENERIC_CLUSTERING_GRAPH_H

#include "openzl/codecs/zl_clustering.h"
#include "openzl/shared/a1cbor.h"
#include "openzl/zl_cgraph.h"
#include "openzl/zl_data.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define ZL_GENERIC_CLUSTERING_CONFIG_ID 315
#define ZL_GENERIC_CLUSTERING_CONFIG_SIZE_ID 316

#define ZL_GENERIC_CLUSTERING_GRAPH_MIN_FORMAT_VERSION 18

ZL_RESULT_DECLARE_TYPE(ZL_ClusteringConfig);

// Clustering
// A graph to be used in conjunction with training. Clusters inputs and sends
// the clusters to successors specified by the configuration.
#define ZL_GRAPH_CLUSTERING           \
    (ZL_GraphID)                      \
    {                                 \
        ZL_StandardGraphID_clustering \
    }

// Note: dst is expected to be reassigned

/** @brief Serializes the @p config into @p dst, using @p arena for allocations.
 * @returns The ZL_Report result of the serialization. Returns an error if the
 * config is malformed or if any allocation failure happens.
 * @param dst Returns a pointer to the address of the buffer containing the
 * result of serialization.
 * @param dstSize Returns a pointer to the size of the buffer for @p dst
 * @param config The config to be serialized
 * @param arena The arena in which memory allocations for serialization happen
 */

ZL_Report ZL_Clustering_serializeClusteringConfig(
        ZL_ErrorContext* errCtx,
        uint8_t** dst,
        size_t* dstSize,
        const ZL_ClusteringConfig* config,
        A1C_Arena* arena);

/** @brief Deserializes the @p config and returns the result using @p arena for
 * allocations.
 * @returns Failure if the config is invalid or an allocation fails. On success
 * returns success status and the deserialized config.
 * @param config The config to be deserialized
 * @param configSize The size of @p config
 * @param arena The arena in which memory allocations for serialization happen
 */
ZL_RESULT_OF(ZL_ClusteringConfig)
ZL_Clustering_deserializeClusteringConfig(
        ZL_ErrorContext* errCtx,
        const uint8_t* config,
        size_t configSize,
        A1C_Arena* arena);

ZL_Report
graph_compressClustered(ZL_Graph* graph, ZL_Edge* inputs[], size_t nbInputs);

#define MIGRAPH_CLUSTERING                                     \
    { .name                = "!zl.cluster",                    \
      .graph_f             = graph_compressClustered,          \
      .inputTypeMasks      = (const ZL_Type[]){ ZL_Type_any }, \
      .nbInputs            = 1,                                \
      .lastInputIsVariable = 1 }

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_GENERIC_CLUSTERING_GRAPH_H
