// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef OPENZL_CODECS_PARTITION_COMMON_PARTITION_H
#define OPENZL_CODECS_PARTITION_COMMON_PARTITION_H

#include "openzl/codecs/zl_partition.h"
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_localParams.h"

ZL_BEGIN_C_DECLS

/**
 * Runtime parameters for the partition codec.
 * @param startValue The start value for the first partition.
 * @param numPartitions The number of partitions.
 * @param partitionSizes The size of each partition.
 */
typedef struct {
    uint64_t startValue;
    size_t numPartitions;
    const uint64_t* partitionSizes;
} ZL_PartitionParams;

bool ZL_PartitionParams_validate(const ZL_PartitionParams* params);

/// Check if all partition sizes are powers of 2.
bool ZL_PartitionParams_areAllSizesPow2(const ZL_PartitionParams* params);

/// Get a preset's partition parameters.
/// @returns A pointer to the preset's static params, or NULL if invalid.
const ZL_PartitionParams* ZL_PartitionParams_getPreset(
        ZL_PartitionParamsPreset preset);

/// Compute the number of extra bits needed per partition.
/// bits[i] = ceil(log2(partitionSizes[i]))
void ZL_PartitionParams_computeBits(
        const ZL_PartitionParams* params,
        uint8_t* bits);

/// Compute the base value for each partition.
/// bases[i] = startValue + sum(partitionSizes[0..i-1])
void ZL_PartitionParams_computeBasesU64(
        const ZL_PartitionParams* params,
        uint64_t* bases);

/// @returns The largest partition size.
uint64_t ZL_PartitionParams_getLargestPartitionSize(
        const ZL_PartitionParams* params);

/// @returns The number of trailing zeros shared by the start value and all the
/// partition sizes.
/// @note This is useful for encoding to reduce the size of the
/// offset->partition LUT. Offsets can be right shifted by this amount, because
/// partition boundaries can only happen at multiples of 2^NumTrailingZeros.
size_t ZL_PartitionParams_getNumTrailingZeros(const ZL_PartitionParams* params);

typedef struct {
    void* opaque;
    void* (*alloc)(void* opaque, size_t size);
} ZL_PartitionScratchAlloc;

/// The maximum partition size where encode & decode can unroll the loop 4 times
/// when reading and writing the offset bits.
#define ZL_PARTITION_MAX_PARTITION_SIZE_FOR_UNROLL4 (1u << 14)

/// Header flag bits for the partition codec header byte.
/// Bits [1:0]: log2(element width in bytes).

/// Bit 2: params are a preset (remaining bits encode preset ID).
#define ZL_PARTITION_HEADER_IS_PRESET_BIT 0x4
/// Bit 3: startValue is 0 (omitted from header).
#define ZL_PARTITION_HEADER_IS_FIRST_VALUE_ZERO_BIT 0x8
/// Bit 4: unused.
#define ZL_PARTITION_HEADER_UNUSED_BIT 0x10
/// Bit 5: all partition sizes are powers of 2.
#define ZL_PARTITION_HEADER_IS_POW2_BIT 0x20

ZL_END_C_DECLS

#endif
