// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_PARTITION_DECODE_PARTITION_KERNEL_H
#define OPENZL_CODECS_PARTITION_DECODE_PARTITION_KERNEL_H

#include "openzl/codecs/partition/common_partition.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

/**
 * Decodes partition-encoded data.
 *
 * For each bucket ID, reads the corresponding number of extra bits from
 * the bitstream and reconstructs the original value as:
 *   value = partitions[bucket_id] + offset
 *
 * @param dst Output buffer for decoded values.
 * @param eltWidth Element width in bytes (1, 2, 4, or 8).
 * @param buckets Input bucket IDs (one per value).
 * @param nbValues Number of values to decode.
 * @param bits Input bitstream containing the extra bits.
 * @param bitsSize Size of the bitstream in bytes.
 * @param params Partition parameters.
 * @returns Success or an error code.
 */
ZL_Report ZL_partitionDecode(
        void* dst,
        size_t eltWidth,
        uint8_t const* buckets,
        size_t nbValues,
        uint8_t const* bits,
        size_t bitsSize,
        ZL_PartitionParams const* params);

ZL_END_C_DECLS

#endif
