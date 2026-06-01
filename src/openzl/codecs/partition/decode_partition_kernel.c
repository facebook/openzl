// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/partition/decode_partition_kernel.h"

#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/shared/mem.h"
#include "openzl/zl_errors.h"

/// The bitstream accumulator holds 63 bits, and after a reload up to 7 bits
/// may be consumed. A single read must satisfy: residual + nbBits <= 64, so the
/// maximum safe read after reload is 57 bits. Use 56 to match the encoder.
#define ZL_PARTITION_BITS_SPLIT 56

/// Read an offset value from the bitstream, splitting into two reads if the
/// number of bits exceeds ZL_PARTITION_BITS_SPLIT.
static uint64_t ZL_partitionReadBits(ZS_BitDStreamFF* bitstream, size_t nbBits)
{
    if (nbBits <= ZL_PARTITION_BITS_SPLIT) {
        return (uint64_t)ZS_BitDStreamFF_read(bitstream, nbBits);
    } else {
        uint64_t const lo = (uint64_t)ZS_BitDStreamFF_read(bitstream, 32);
        ZS_BitDStreamFF_reload(bitstream);
        uint64_t const hi =
                (uint64_t)ZS_BitDStreamFF_read(bitstream, nbBits - 32);
        return lo | (hi << 32);
    }
}

static uint64_t ZL_partitionDecode1(
        uint8_t bucket,
        ZS_BitDStreamFF* bitstream,
        uint64_t const* partitions,
        uint8_t const* bits)
{
    uint64_t const offset = ZL_partitionReadBits(bitstream, bits[bucket]);
    return partitions[bucket] + offset;
}

/// Write a decoded value to the output buffer at the given index.
static void
ZL_writeValue(void* dst, size_t index, uint64_t value, size_t eltWidth)
{
    switch (eltWidth) {
        case 1:
            ((uint8_t*)dst)[index] = (uint8_t)value;
            break;
        case 2:
            ((uint16_t*)dst)[index] = (uint16_t)value;
            break;
        case 4:
            ((uint32_t*)dst)[index] = (uint32_t)value;
            break;
        case 8:
            ((uint64_t*)dst)[index] = value;
            break;
    }
}
ZL_Report ZL_partitionDecode(
        void* dst,
        size_t eltWidth,
        uint8_t const* buckets,
        size_t nbValues,
        uint8_t const* offsets,
        size_t offsetsSize,
        ZL_PartitionParams const* params)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ASSERT(ZL_PartitionParams_validate(params));

    uint64_t bases[ZL_PARTITION_MAX_PARTITIONS];
    uint8_t bits[ZL_PARTITION_MAX_PARTITIONS];
    ZL_PartitionParams_computeBasesU64(params, bases);
    ZL_PartitionParams_computeBits(params, bits);

    ZS_BitDStreamFF bitstream = ZS_BitDStreamFF_init(offsets, offsetsSize);

    for (size_t i = 0; i < nbValues; ++i) {
        uint64_t const val =
                ZL_partitionDecode1(buckets[i], &bitstream, bases, bits);
        ZL_writeValue(dst, i, val, eltWidth);
        ZS_BitDStreamFF_reload(&bitstream);
    }

    ZL_Report const ret = ZS_BitDStreamFF_finish(&bitstream);
    ZL_ERR_IF(ZL_isError(ret), srcSize_tooSmall);

    return ZL_returnSuccess();
}
