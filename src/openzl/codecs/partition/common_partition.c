// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/partition/common_partition.h"

#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/codecs/zl_partition.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/overflow.h"
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"

bool ZL_PartitionParams_validate(const ZL_PartitionParams* params)
{
    if (params->numPartitions == 0) {
        // Invalid: Can only work on empty data => store
        return false;
    }
    if (params->numPartitions > 256) {
        // Invalid: Too many partitions
        return false;
    }
    if (params->numPartitions == 1 && params->startValue == 0) {
        // Invalid: No-op
        return false;
    }
    uint64_t sum = params->startValue;
    for (size_t i = 0; i < params->numPartitions; ++i) {
        if (params->partitionSizes[i] == 0) {
            // Invalid: Partitions cannot be empty
            return false;
        }
        if (ZL_overflowAddU64(sum, params->partitionSizes[i], &sum)) {
            if (!(i + 1 == params->numPartitions && sum == 0)) {
                // Invalid: Either non-final partition overflows, or final
                // partition does not exactly sum to 2^64.
                return false;
            }
        }
    }
    return true;
}

bool ZL_PartitionParams_areAllSizesPow2(const ZL_PartitionParams* params)
{
    for (size_t i = 0; i < params->numPartitions; ++i) {
        if (!ZL_isPow2(params->partitionSizes[i])) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Preset: Quantize Offsets (preset 0)
// Pure power-of-2 scheme: [1, 2), [2, 4), [4, 8), ..., [2^31, 2^32)
// Value 0 cannot be encoded.
// ---------------------------------------------------------------------------
static uint64_t const quantizeOffsetsPartitionSizes[32] = {
    0x1,        0x2,        0x4,       0x8,       0x10,       0x20,
    0x40,       0x80,       0x100,     0x200,     0x400,      0x800,
    0x1000,     0x2000,     0x4000,    0x8000,    0x10000,    0x20000,
    0x40000,    0x80000,    0x100000,  0x200000,  0x400000,   0x800000,
    0x1000000,  0x2000000,  0x4000000, 0x8000000, 0x10000000, 0x20000000,
    0x40000000, 0x80000000,
};

static ZL_PartitionParams const quantizeOffsetsParams = {
    .startValue     = 1,
    .numPartitions  = 32,
    .partitionSizes = quantizeOffsetsPartitionSizes,
};

// ---------------------------------------------------------------------------
// Preset: Quantize Lengths (preset 1)
// Values 0-15 each get their own bucket (0 extra bits).
// Then power-of-2 scheme: [16, 32), [32, 64), ..., [2^31, 2^32)
// ---------------------------------------------------------------------------
static uint64_t const quantizeLengthsPartitionSizess[44] = {
    0x1,        0x1,        0x1,       0x1,       0x1,        0x1,
    0x1,        0x1,        0x1,       0x1,       0x1,        0x1,
    0x1,        0x1,        0x1,       0x1,       0x10,       0x20,
    0x40,       0x80,       0x100,     0x200,     0x400,      0x800,
    0x1000,     0x2000,     0x4000,    0x8000,    0x10000,    0x20000,
    0x40000,    0x80000,    0x100000,  0x200000,  0x400000,   0x800000,
    0x1000000,  0x2000000,  0x4000000, 0x8000000, 0x10000000, 0x20000000,
    0x40000000, 0x80000000,
};

static ZL_PartitionParams const quantizeLengthsParams = {
    .startValue     = 0,
    .numPartitions  = 44,
    .partitionSizes = quantizeLengthsPartitionSizess,
};

// ---------------------------------------------------------------------------
// Preset: Varbyte16 (preset 2)
// [0, 2), [2, 4), [4, 8), [8, 16), ..., [0x8000, 0x10000)
// ---------------------------------------------------------------------------
static uint64_t const varbyte16PartitionSizes[16] = {
    0x2,   0x2,   0x4,   0x8,   0x10,   0x20,   0x40,   0x80,
    0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000,
};

static ZL_PartitionParams const varbyte16Params = {
    .startValue     = 0,
    .numPartitions  = 16,
    .partitionSizes = varbyte16PartitionSizes,
};

#if 0
// ---------------------------------------------------------------------------
// Preset: Varbyte32 (disabled)
// Can only encode values < 0x1558000
// ---------------------------------------------------------------------------
static uint64_t const varbyte32PartitionSizes[16] = {
    0x20,   0x20,   0x40,   0x80,    0x100,   0x200,    0x400,    0x800,
    0x1000, 0x2000, 0x4000, 0x10000, 0x40000, 0x100000, 0x400000, 0x1000000,
};

static ZL_PartitionParams const varbyte32Params = {
    .startValue     = 0,
    .numPartitions  = 16,
    .partitionSizes = varbyte32PartitionSizes,
};
#endif

static ZL_PartitionParams const* const
        presetTable[ZL_PartitionParamsPreset_custom] = {
            &quantizeOffsetsParams,
            &quantizeLengthsParams,
            &varbyte16Params,
        };

const ZL_PartitionParams* ZL_PartitionParams_getPreset(
        ZL_PartitionParamsPreset preset)
{
    if ((int)preset < 0 || (int)preset >= ZL_PartitionParamsPreset_custom) {
        return NULL;
    }
    return presetTable[preset];
}

void ZL_PartitionParams_computeBits(
        const ZL_PartitionParams* params,
        uint8_t* bits)
{
    for (size_t i = 0; i < params->numPartitions; ++i) {
        bits[i] = (uint8_t)ZL_nextPow2(params->partitionSizes[i]);
    }
}

void ZL_PartitionParams_computeBasesU64(
        const ZL_PartitionParams* params,
        uint64_t* bases)
{
    bases[0] = params->startValue;
    for (size_t i = 1; i < params->numPartitions; ++i) {
        bases[i] = bases[i - 1] + params->partitionSizes[i - 1];
    }
}

uint64_t ZL_PartitionParams_getLargestPartitionSize(
        const ZL_PartitionParams* params)
{
    uint64_t max = 0;
    for (size_t i = 0; i < params->numPartitions; ++i) {
        max = ZL_MAX(max, params->partitionSizes[i]);
    }
    return max;
}

size_t ZL_PartitionParams_getNumTrailingZeros(const ZL_PartitionParams* params)
{
    ZL_ASSERT(ZL_PartitionParams_validate(params));
    int numTrailingZeros =
            params->startValue == 0 ? 64 : ZL_ctz64(params->startValue);

    for (size_t i = 0; i < params->numPartitions; ++i) {
        numTrailingZeros =
                ZL_MIN(numTrailingZeros, ZL_ctz64(params->partitionSizes[i]));
    }
    ZL_ASSERT_LT(numTrailingZeros, 64);
    return (size_t)numTrailingZeros;
}

ZL_Report ZL_PartitionParams_parseHeader(
        ZL_PartitionParams* params,
        size_t* width,
        const uint8_t* header,
        size_t headerSize,
        uint64_t* partitionSizesBuffer)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);

    ZL_ERR_IF_LT(headerSize, 1, corruption, "Empty header");
    const uint8_t* hdr       = header;
    const uint8_t* const end = hdr + headerSize;
    const uint8_t flags      = *hdr++;

    *width = 1u << (flags & 0x3);

    if (flags & ZL_PARTITION_HEADER_IS_PRESET_BIT) {
        const ZL_PartitionParamsPreset preset =
                (ZL_PartitionParamsPreset)(flags >> 3);
        ZL_PartitionParams const* const presetParams =
                ZL_PartitionParams_getPreset(preset);
        ZL_ERR_IF_NULL(presetParams, corruption);
        *params = *presetParams;
        return ZL_returnSuccess();
    }

    if (flags & ZL_PARTITION_HEADER_IS_FIRST_VALUE_ZERO_BIT) {
        params->startValue = 0;
    } else {
        ZL_TRY_SET(uint64_t, params->startValue, ZL_varintDecode(&hdr, end));
    }

    if (flags & ZL_PARTITION_HEADER_IS_POW2_BIT) {
        const size_t numBits = (size_t)(flags >> 6) + 3;
        ZL_ERR_IF_EQ(hdr, end, corruption, "Missing partition sizes");
        ZL_ERR_IF_EQ(
                end[-1], 0, corruption, "Corrupted partition sizes bitstream");
        const size_t unusedBits = 8 - (size_t)ZL_highbit32(end[-1]);
        const size_t totalBits  = 8 * (size_t)(end - hdr) - unusedBits;
        ZL_ERR_IF_NE(
                totalBits % numBits,
                0,
                corruption,
                "bitstream size not multiple of numBits");
        params->numPartitions = totalBits / numBits;

        ZL_ERR_IF_GT(
                params->numPartitions, ZL_PARTITION_MAX_PARTITIONS, corruption);

        ZS_BitDStreamFF bitstream =
                ZS_BitDStreamFF_init(hdr, (size_t)(end - hdr));
        for (size_t i = 0; i < params->numPartitions; ++i) {
            uint64_t const log2Size = ZS_BitDStreamFF_read(&bitstream, numBits);
            partitionSizesBuffer[i] = 1ULL << log2Size;
            ZS_BitDStreamFF_reload(&bitstream);
        }

        ZL_ERR_IF_ERR(ZS_BitDStreamFF_finish(&bitstream));
        params->partitionSizes = partitionSizesBuffer;
    } else {
        params->numPartitions = 0;
        while (hdr < end) {
            ZL_ERR_IF_GE(
                    params->numPartitions,
                    ZL_PARTITION_MAX_PARTITIONS,
                    corruption);
            ZL_TRY_SET(
                    uint64_t,
                    partitionSizesBuffer[params->numPartitions++],
                    ZL_varintDecode(&hdr, end));
        }
        params->partitionSizes = partitionSizesBuffer;
    }
    ZL_ERR_IF_NOT(ZL_PartitionParams_validate(params), corruption);
    return ZL_returnSuccess();
}
