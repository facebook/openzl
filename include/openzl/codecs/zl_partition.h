// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_CODECS_ZL_PARTITION_H
#define OPENZL_CODECS_ZL_PARTITION_H

#include <stdint.h>

#include "openzl/zl_errors.h"
#include "openzl/zl_graphs.h"
#include "openzl/zl_nodes.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Partitions unsigned integers into buckets and extra bits.
 *
 * Each value is assigned a bucket based on a configurable list of partition
 * boundaries. The extra bits encode the offset within the bucket using
 * ceil(log2(bucket_size)) bits per value.
 *
 * Presets are available for common partitioning schemes.
 * Custom partition lists can be provided via ZL_PARTITION_CUSTOM_PID.
 *
 * Input: Numeric unsigned integers (1, 2, 4, or 8 bytes)
 * Output 0: Numeric 8-bit unsigned bucket IDs
 * Output 1: Serial extra-bits
 */
#define ZL_NODE_PARTITION ZL_MAKE_NODE_ID(ZL_StandardNodeID_partition)

/// Maximum number of buckets supported by the partition codec.
#define ZL_PARTITION_MAX_PARTITIONS 256

/// Int param key selecting a ZL_PartitionPreset value.
#define ZL_PARTITION_PRESET_PID 120

/**
 * Copy param key for custom partition settings
 *
 * The format is:
 *   [uint64_t] starting value for the first partition
 *   N * [uint64_t] partition size
 *
 * Requirements:
 * - At least one partition
 * - If there is only one partition, then the starting value must not be zero
 * - Each partition size must be > 0
 * - The sum of the partition sizes must be <= 2^64
 * - If the partitioning does not cover the entire range of inputs,
 *   the input MUST NOT contain values outside of the range, otherwise
 *   compression will fail.
 */
#define ZL_PARTITION_CUSTOM_PID 121

/// Preset partition IDs for the codec header.
typedef enum {
    ZL_PartitionParamsPreset_quantizeOffsets = 0,
    ZL_PartitionParamsPreset_quantizeLengths = 1,
    ZL_PartitionParamsPreset_varbyte16       = 2,
    ZL_PartitionParamsPreset_custom,
} ZL_PartitionParamsPreset;

/// Build a partition node using a preset partitioning scheme.
ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_buildPartitionNodeFromPreset(
        ZL_Compressor* compressor,
        ZL_PartitionParamsPreset preset);

/// Build a partition node with a custom partition list.
ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_buildPartitionNode(
        ZL_Compressor* compressor,
        uint64_t startValue,
        const uint64_t* partitionSizes,
        size_t numPartitions);

/// If set to ZL_TernaryParam_auto, use default behavior
/// If set to ZL_TernaryParam_enable, enable optimal mode (slower for slightly
/// better compression)
/// If set to ZL_TernaryParam_disable, disable optimal mode (much faster but
/// slightly worse compression)
#define ZL_GRAPH_PARTITION_BITPACK_OPTIMAL_PID 0

/// Graph that computes partition boundaries for 16-bit numeric data,
/// routing bucket IDs to ZL_GRAPH_BITPACK and offsets to ZL_GRAPH_STORE.
#define ZL_GRAPH_PARTITION_BITPACK \
    ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_partition_bitpack)

#if defined(__cplusplus)
}
#endif

#endif
