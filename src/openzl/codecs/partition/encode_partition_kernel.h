// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_PARTITION_ENCODE_PARTITION_KERNEL_H
#define OPENZL_CODECS_PARTITION_ENCODE_PARTITION_KERNEL_H

#include "openzl/codecs/partition/common_partition.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

/**
 * Encodes values using the partition scheme.
 *
 * Each value is split into a bucket ID (written to @p buckets) and an offset
 * within the bucket (written to the bitstream at @p bitsDst). The number of
 * bits per offset is determined by the bucket size (params->bits[bucket]).
 *
 * @param bitsDst Output buffer for the extra-bits bitstream.
 * @param bitsCapacity Capacity of the bits buffer in bytes.
 * @param buckets Output buffer for bucket IDs, must hold @p srcSize bytes.
 * @param src Input values to encode.
 * @param srcSize Number of input values.
 * @param eltWidth Element width in bytes (1, 2, 4, or 8).
 * @param params Partition parameters (start value and partition sizes).
 * @returns The size of the bits stream in bytes, or an error.
 */
ZL_Report ZL_partitionEncode(
        uint8_t* bitsDst,
        size_t bitsCapacity,
        uint8_t* buckets,
        void const* src,
        size_t srcSize,
        size_t eltWidth,
        ZL_PartitionParams const* params,
        ZL_PartitionScratchAlloc scratch);

ZL_END_C_DECLS

#endif
