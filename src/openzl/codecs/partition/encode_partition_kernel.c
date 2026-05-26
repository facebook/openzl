// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/partition/encode_partition_kernel.h"

#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/numeric_operations.h"
#include "openzl/shared/utils.h"
#include "openzl/zl_errors.h"

#define PB_U32_LOG_BUCKETS (32 - PB_LINEAR_THRESHOLD_LOG)

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

/// Generic partition encoding using binary search per element.
static ZL_Report ZL_partitionEncode_generic(
        uint8_t* bitsDst,
        size_t bitsCapacity,
        uint8_t* buckets,
        void const* src,
        size_t srcSize,
        size_t eltWidth,
        ZL_PartitionParams const* params)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);

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

static const uint8_t* ZL_PartitionEncode_buildPartitionLUT(
        ZL_PartitionParams const* params,
        ZL_PartitionScratchAlloc scratch,
        size_t lutShift,
        size_t lutSize)
{
    uint8_t* partitionLUT = scratch.alloc(scratch.opaque, lutSize);
    if (partitionLUT == NULL) {
        return NULL;
    }

    {
        size_t start =
                ZL_MIN(lutSize, (size_t)(params->startValue >> lutShift));
        memset(partitionLUT, 0, start);
        for (size_t i = 0; i < params->numPartitions; ++i) {
            if (start >= lutSize) {
                break;
            }
            const size_t end = ZL_MIN(
                    lutSize,
                    start + (size_t)(params->partitionSizes[i] >> lutShift));
            memset(partitionLUT + start, (int)i, end - start);
            start = end;
        }
        if (start < lutSize) {
            memset(partitionLUT + start, 0, lutSize - start);
        }
    }
    return partitionLUT;
}

/// Map a u32 value to its partition index using the hybrid LUT.
/// Below the linear threshold: direct LUT lookup (same as u16).
/// At or above: index by floor(log2(value)), which works because partition
/// boundaries above the threshold are constrained to powers of two.
static size_t ZL_PartitionEncode_u32Partition(
        uint32_t value,
        const uint8_t* partitionLUT,
        const uint8_t* logPartitions,
        size_t lutShift)
{
    if (value < PB_LINEAR_THRESHOLD) {
        return partitionLUT[value >> lutShift];
    }
    return logPartitions[ZL_highbit32(value) - PB_LINEAR_THRESHOLD_LOG];
}

/// The hybrid LUT requires partition boundaries above the linear threshold to
/// be distinct powers of two, so that floor(log2(value)) uniquely identifies
/// the partition. Within the linear range, any boundary is fine.
static bool ZL_PartitionEncode_u32BoundarySupported(
        uint64_t value,
        uint64_t prevPow2Boundary)
{
    if (value <= PB_LINEAR_THRESHOLD) {
        return true;
    }
    return value <= (1ULL << 32) && ZL_isPow2(value)
            && value > prevPow2Boundary;
}

static bool ZL_PartitionEncodeU32_canUseHybridLUT(
        ZL_PartitionParams const* params)
{
    uint64_t boundary         = params->startValue;
    uint64_t prevPow2Boundary = 0;
    if (!ZL_PartitionEncode_u32BoundarySupported(boundary, prevPow2Boundary)) {
        return false;
    }
    for (size_t i = 0; i < params->numPartitions; ++i) {
        if (UINT64_MAX - boundary < params->partitionSizes[i]) {
            return false;
        }
        if (boundary > PB_LINEAR_THRESHOLD) {
            prevPow2Boundary = boundary;
        }
        boundary += params->partitionSizes[i];
        if (i + 1 < params->numPartitions
            && !ZL_PartitionEncode_u32BoundarySupported(
                    boundary, prevPow2Boundary)) {
            return false;
        }
    }
    return boundary <= (1ULL << 32);
}

/// Expanded LUT for batched encoding: precompute base and mask for each
/// pair of bucket IDs. This allows processing 4 elements (64 bits) at once.
typedef struct {
    const uint32_t* base;
    const uint32_t* mask;
    const uint8_t* bits;
} ZL_PartitionLUTx2;

/// Build expanded LUT indexed by pairs of bucket IDs.
/// For a pair (lo, hi) at index (lo | hi << partitionBits):
///   base[idx] = baseLUT[lo] | (baseLUT[hi] << 16)
///   mask[idx] = ((1 << bitsLUT[lo]) - 1) | (((1 << bitsLUT[hi]) - 1) << 16)
///   bits[idx] = bitsLUT[lo] + bitsLUT[hi]
static bool ZL_PartitionLUTx2_build(
        ZL_PartitionLUTx2* LUTx2,
        const uint64_t* baseLUT,
        const uint8_t* bitsLUT,
        size_t partitionBits,
        ZL_PartitionScratchAlloc scratch)
{
    ZL_ASSERT_LE(partitionBits, 8);
    size_t const sizeX1        = (size_t)1 << partitionBits;
    size_t const sizeX2        = (size_t)1 << (2 * partitionBits);
    size_t const partitionMask = sizeX1 - 1;

    uint32_t* base =
            scratch.alloc(scratch.opaque, sizeX2 * sizeof(*LUTx2->base));
    uint32_t* mask =
            scratch.alloc(scratch.opaque, sizeX2 * sizeof(*LUTx2->mask));
    uint8_t* bits =
            scratch.alloc(scratch.opaque, sizeX2 * sizeof(*LUTx2->bits));

    if (!base || !mask || !bits) {
        return false;
    }

    for (size_t idx = 0; idx < sizeX2; ++idx) {
        const size_t lo = idx & partitionMask;
        const size_t hi = idx >> partitionBits;

        base[idx] = (uint32_t)baseLUT[lo] | ((uint32_t)baseLUT[hi] << 16);
        mask[idx] =
                ((1u << bitsLUT[lo]) - 1) | (((1u << bitsLUT[hi]) - 1) << 16);
        bits[idx] = bitsLUT[lo] + bitsLUT[hi];
    }

    LUTx2->base = base;
    LUTx2->mask = mask;
    LUTx2->bits = bits;

    return true;
}

/// Fast path for 2-byte elements with batched encoding.
/// Uses expanded LUTs and ZL_bitExtract64 for efficient bit packing,
/// processing 4 elements at a time.
static ZL_Report ZL_partitionEncodeU16(
        uint8_t* offPtr,
        size_t offCapacity,
        uint8_t* partitions,
        uint16_t const* src,
        size_t srcSize,
        ZL_PartitionParams const* params,
        ZL_PartitionScratchAlloc scratch)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);

    uint64_t baseLUTx1[ZL_PARTITION_MAX_PARTITIONS] = { 0 };
    uint8_t bitsLUTx1[ZL_PARTITION_MAX_PARTITIONS]  = { 0 };
    ZL_PartitionParams_computeBasesU64(params, baseLUTx1);
    ZL_PartitionParams_computeBits(params, bitsLUTx1);

    // Build per-value LUTs for the partition
    const size_t lutShift = ZL_PartitionParams_getNumTrailingZeros(params);
    const size_t lutSize  = 65536 >> lutShift;
    const uint8_t* const partitionLUT = ZL_PartitionEncode_buildPartitionLUT(
            params, scratch, lutShift, lutSize);
    ZL_ERR_IF_NULL(partitionLUT, allocation);

    // If the partition doesn't cover the entire U16 range, validate that every
    // input is covered by the partitions.
    const uint64_t minValue = params->startValue;
    const uint64_t maxValue = baseLUTx1[params->numPartitions - 1]
            + (params->partitionSizes[params->numPartitions - 1] - 1);
    if (minValue > 0 || maxValue < UINT16_MAX) {
        bool isValid = true;
        for (size_t i = 0; i < srcSize; ++i) {
            if (src[i] < minValue || src[i] > maxValue) {
                isValid = false;
            }
        }
        ZL_ERR_IF_NOT(
                isValid,
                node_invalid_input,
                "Value out of range of partitions");
    }

    // Determine bucket bit width (ceil(log2(numPartitions)))
    const size_t bucketBits = (size_t)ZL_nextPow2(params->numPartitions);

    // Build expanded LUT for batched 4-element processing
    ZL_PartitionLUTx2 LUTx2;
    ZL_ERR_IF_NOT(
            ZL_PartitionLUTx2_build(
                    &LUTx2, baseLUTx1, bitsLUTx1, bucketBits, scratch),
            allocation);

    // Main encoding loop: process 4 elements at a time
    ZS_BitCStreamFF off = ZS_BitCStreamFF_init(offPtr, offCapacity);
    size_t i            = 0;

    const size_t limit = srcSize < 4 ? 0 : srcSize - 3;
    for (; i < limit; i += 4) {
        ZL_ASSERT_LE(i + 4, srcSize);
        const uint16_t* const ip = src + i;

        // Lookup bucket IDs
        const size_t p0 = partitionLUT[ip[0] >> lutShift];
        const size_t p1 = partitionLUT[ip[1] >> lutShift];
        const size_t p2 = partitionLUT[ip[2] >> lutShift];
        const size_t p3 = partitionLUT[ip[3] >> lutShift];

        partitions[i + 0] = (uint8_t)p0;
        partitions[i + 1] = (uint8_t)p1;
        partitions[i + 2] = (uint8_t)p2;
        partitions[i + 3] = (uint8_t)p3;

        // Pack bucket pairs for expanded LUT lookup
        const size_t p01 = p0 | (p1 << bucketBits);
        const size_t p23 = p2 | (p3 << bucketBits);

        // Compute bases and masks for all 4 elements via expanded LUT
        const uint64_t base =
                LUTx2.base[p01] | ((uint64_t)LUTx2.base[p23] << 32);
        const uint64_t mask =
                LUTx2.mask[p01] | ((uint64_t)LUTx2.mask[p23] << 32);
        const size_t bits = LUTx2.bits[p01] + LUTx2.bits[p23];

        // Read 4 uint16_t values as one uint64_t
        const uint64_t data = ZL_read64(ip);

        // Extract offset bits using PEXT
        uint64_t const packedOffsetBits = ZL_bitExtract64(data - base, mask);

        // Accumulate and flush
        ZS_BitCStreamFF_write(&off, packedOffsetBits, bits);
        ZS_BitCStreamFF_flush(&off);
    }

    // Process remaining elements one at a time
    for (; i < srcSize; ++i) {
        uint16_t const value  = src[i];
        const size_t p        = partitionLUT[value >> lutShift];
        partitions[i]         = (uint8_t)p;
        uint64_t const offset = value - baseLUTx1[p];
        size_t const nbits    = bitsLUTx1[p];
        ZS_BitCStreamFF_write(&off, offset, nbits);
        ZS_BitCStreamFF_flush(&off);
    }
    ZL_TRY_LET(size_t, offSize, ZS_BitCStreamFF_finish(&off));
    return ZL_returnValue(offSize);
}

typedef struct {
    const uint64_t* base;
    const uint64_t* mask;
    const uint8_t* bits;
} ZL_PartitionLUTx2U32;

static bool ZL_PartitionLUTx2U32_build(
        ZL_PartitionLUTx2U32* LUTx2,
        const uint64_t* baseLUT,
        const uint8_t* bitsLUT,
        size_t partitionBits,
        ZL_PartitionScratchAlloc scratch)
{
    ZL_ASSERT_LE(partitionBits, 8);
    size_t const sizeX1        = (size_t)1 << partitionBits;
    size_t const sizeX2        = (size_t)1 << (2 * partitionBits);
    size_t const partitionMask = sizeX1 - 1;

    uint64_t* base =
            scratch.alloc(scratch.opaque, sizeX2 * sizeof(*LUTx2->base));
    uint64_t* mask =
            scratch.alloc(scratch.opaque, sizeX2 * sizeof(*LUTx2->mask));
    uint8_t* bits =
            scratch.alloc(scratch.opaque, sizeX2 * sizeof(*LUTx2->bits));

    if (!base || !mask || !bits) {
        return false;
    }

    for (size_t idx = 0; idx < sizeX2; ++idx) {
        const size_t lo = idx & partitionMask;
        const size_t hi = idx >> partitionBits;

        base[idx] = (uint64_t)(uint32_t)baseLUT[lo]
                | ((uint64_t)(uint32_t)baseLUT[hi] << 32);
        mask[idx] = ((1ULL << bitsLUT[lo]) - 1)
                | (((1ULL << bitsLUT[hi]) - 1) << 32);
        bits[idx] = bitsLUT[lo] + bitsLUT[hi];
    }

    LUTx2->base = base;
    LUTx2->mask = mask;
    LUTx2->bits = bits;

    return true;
}

static ZL_Report ZL_partitionEncodeU32(
        uint8_t* offPtr,
        size_t offCapacity,
        uint8_t* partitions,
        uint32_t const* src,
        size_t srcSize,
        ZL_PartitionParams const* params,
        ZL_PartitionScratchAlloc scratch)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);

    uint64_t baseLUTx1[ZL_PARTITION_MAX_PARTITIONS] = { 0 };
    uint8_t bitsLUTx1[ZL_PARTITION_MAX_PARTITIONS]  = { 0 };
    ZL_PartitionParams_computeBasesU64(params, baseLUTx1);
    ZL_PartitionParams_computeBits(params, bitsLUTx1);

    const size_t lutShift = ZL_PartitionParams_getNumTrailingZeros(params);
    const size_t lutSize  = ZL_MAX((size_t)1, PB_LINEAR_THRESHOLD >> lutShift);
    const uint8_t* const partitionLUT = ZL_PartitionEncode_buildPartitionLUT(
            params, scratch, lutShift, lutSize);
    ZL_ERR_IF_NULL(partitionLUT, allocation);

    const size_t numPartitions = params->numPartitions;
    const uint64_t minValue    = params->startValue;
    const uint64_t maxValue    = baseLUTx1[numPartitions - 1]
            + (params->partitionSizes[numPartitions - 1] - 1);
    if (minValue > 0 || maxValue < UINT32_MAX) {
        bool isValid = true;
        for (size_t i = 0; i < srcSize; ++i) {
            if (src[i] < minValue || src[i] > maxValue) {
                isValid = false;
            }
        }
        ZL_ERR_IF_NOT(
                isValid,
                node_invalid_input,
                "Value out of range of partitions");
    }

    uint8_t logPartitions[PB_U32_LOG_BUCKETS] = { 0 };
    for (size_t i = 0; i < PB_U32_LOG_BUCKETS; ++i) {
        const uint64_t value = 1ULL << (PB_LINEAR_THRESHOLD_LOG + i);
        const size_t partition =
                ZL_findBucket(value, baseLUTx1, numPartitions, maxValue);
        logPartitions[i] = partition < numPartitions ? (uint8_t)partition : 0;
    }

    const size_t bucketBits = (size_t)ZL_nextPow2(numPartitions);

    ZL_PartitionLUTx2U32 LUTx2;
    ZL_ERR_IF_NOT(
            ZL_PartitionLUTx2U32_build(
                    &LUTx2, baseLUTx1, bitsLUTx1, bucketBits, scratch),
            allocation);

    ZS_BitCStreamFF off = ZS_BitCStreamFF_init(offPtr, offCapacity);
    size_t i            = 0;

    // Encode 2 u32 values at a time: read them as one u64, subtract the
    // packed bases, then PEXT extracts both offset fields in one operation.
    const size_t limit = srcSize < 2 ? 0 : srcSize - 1;
    for (; i < limit; i += 2) {
        ZL_ASSERT_LE(i + 2, srcSize);
        const uint32_t* const ip = src + i;

        const size_t p0 = ZL_PartitionEncode_u32Partition(
                ip[0], partitionLUT, logPartitions, lutShift);
        const size_t p1 = ZL_PartitionEncode_u32Partition(
                ip[1], partitionLUT, logPartitions, lutShift);

        partitions[i + 0] = (uint8_t)p0;
        partitions[i + 1] = (uint8_t)p1;

        const size_t p01    = p0 | (p1 << bucketBits);
        const uint64_t base = LUTx2.base[p01];
        const uint64_t mask = LUTx2.mask[p01];
        const size_t bits   = LUTx2.bits[p01];

        const uint64_t data             = ZL_read64(ip);
        uint64_t const packedOffsetBits = ZL_bitExtract64(data - base, mask);

        ZS_BitCStreamFF_write(&off, packedOffsetBits, bits);
        ZS_BitCStreamFF_flush(&off);
    }

    for (; i < srcSize; ++i) {
        uint32_t const value = src[i];
        const size_t p       = ZL_PartitionEncode_u32Partition(
                value, partitionLUT, logPartitions, lutShift);
        partitions[i]         = (uint8_t)p;
        uint64_t const offset = value - baseLUTx1[p];
        size_t const nbits    = bitsLUTx1[p];
        ZS_BitCStreamFF_write(&off, offset, nbits);
        ZS_BitCStreamFF_flush(&off);
    }
    ZL_TRY_LET(size_t, offSize, ZS_BitCStreamFF_finish(&off));
    return ZL_returnValue(offSize);
}

ZL_Report ZL_partitionEncode(
        uint8_t* bitsDst,
        size_t bitsCapacity,
        uint8_t* buckets,
        void const* src,
        size_t srcSize,
        size_t eltWidth,
        ZL_PartitionParams const* params,
        ZL_PartitionScratchAlloc scratch)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ASSERT(ZL_PartitionParams_validate(params));

    // Fast path for 16-bit elements with partition sizes <= 2^14.
    if (eltWidth == 2
        && ZL_PartitionParams_getLargestPartitionSize(params)
                <= ZL_PARTITION_MAX_PARTITION_SIZE_FOR_UNROLL4) {
        return ZL_partitionEncodeU16(
                bitsDst,
                bitsCapacity,
                buckets,
                (uint16_t const*)src,
                srcSize,
                params,
                scratch);
    }
    if (eltWidth == 4
        && ZL_PartitionParams_getLargestPartitionSize(params)
                <= ZL_PARTITION_MAX_PARTITION_SIZE_FOR_UNROLL2
        && ZL_PartitionEncodeU32_canUseHybridLUT(params)) {
        return ZL_partitionEncodeU32(
                bitsDst,
                bitsCapacity,
                buckets,
                (uint32_t const*)src,
                srcSize,
                params,
                scratch);
    }

    return ZL_partitionEncode_generic(
            bitsDst, bitsCapacity, buckets, src, srcSize, eltWidth, params);
}
