// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/partition/encode_partition_kernel.h"

#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/zl_errors.h"

/// Find the bucket for @p value using binary search.
/// Returns the index i such that partitions[i] <= value, and either
/// i+1 == numBuckets or value < partitions[i+1].
/// Returns numBuckets if the value is out of range.
static size_t ZL_findBucket(
        uint64_t value,
        uint64_t const* partitions,
        size_t numBuckets,
        uint64_t maxValue)
{
    // Check that the value is within the partition range
    if (value < partitions[0] || value > maxValue) {
        return numBuckets; // out of range
    }
    size_t lo = 0;
    size_t hi = numBuckets;
    while (lo < hi) {
        size_t const mid = lo + (hi - lo) / 2;
        if (mid + 1 < numBuckets && partitions[mid + 1] <= value) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/// Read a value from a buffer at the given index with the given element width.
static uint64_t ZL_readValue(void const* src, size_t index, size_t eltWidth)
{
    switch (eltWidth) {
        case 1:
            return ((uint8_t const*)src)[index];
        case 2:
            return ((uint16_t const*)src)[index];
        case 4:
            return ((uint32_t const*)src)[index];
        case 8:
            return ((uint64_t const*)src)[index];
        default:
            return 0;
    }
}

/// The bitstream accumulator holds 63 bits, and after a flush up to 7 bits
/// may remain. A single write must satisfy: residual + nbBits <= 63, so the
/// maximum safe write after flush is 56 bits.
#define ZL_PARTITION_BITS_SPLIT 56

/// Write an offset value into the bitstream, splitting into two writes if the
/// number of bits exceeds ZL_PARTITION_BITS_SPLIT.
static void ZL_partitionWriteBits(
        ZS_BitCStreamFF* bitstream,
        uint64_t offset,
        size_t nbBits)
{
    if (nbBits <= ZL_PARTITION_BITS_SPLIT) {
        ZS_BitCStreamFF_write(bitstream, (size_t)offset, nbBits);
    } else {
        ZS_BitCStreamFF_write(bitstream, (size_t)offset, 32);
        ZS_BitCStreamFF_flush(bitstream);
        ZS_BitCStreamFF_write(bitstream, (size_t)(offset >> 32), nbBits - 32);
    }
}

ZL_Report ZL_partitionEncode(
        uint8_t* bitsDst,
        size_t bitsCapacity,
        uint8_t* buckets,
        void const* src,
        size_t srcSize,
        size_t eltWidth,
        ZL_PartitionParams const* params)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ASSERT(ZL_PartitionParams_validate(params));

    uint64_t bases[ZL_PARTITION_MAX_PARTITIONS];
    uint8_t bits[ZL_PARTITION_MAX_PARTITIONS];
    ZL_PartitionParams_computeBasesU64(params, bases);
    ZL_PartitionParams_computeBits(params, bits);

    size_t const numPartitions = params->numPartitions;
    const uint64_t maxValue    = bases[numPartitions - 1]
            + (params->partitionSizes[numPartitions - 1] - 1);

    ZS_BitCStreamFF bitstream = ZS_BitCStreamFF_init(bitsDst, bitsCapacity);
    for (size_t i = 0; i < srcSize; ++i) {
        uint64_t const value = ZL_readValue(src, i, eltWidth);
        size_t const partition =
                ZL_findBucket(value, bases, numPartitions, maxValue);

        ZL_ERR_IF_GE(partition, numPartitions, GENERIC);

        ZL_ASSERT_GE(value, bases[partition]);
        ZL_ASSERT_LE(value, maxValue);

        buckets[i] = (uint8_t)partition;

        uint64_t const offset = value - bases[partition];
        ZL_partitionWriteBits(&bitstream, offset, bits[partition]);
        ZS_BitCStreamFF_flush(&bitstream);
    }
    ZL_Report const ret = ZS_BitCStreamFF_finish(&bitstream);
    ZL_ERR_IF(ZL_isError(ret), internalBuffer_tooSmall);
    return ret;
}
