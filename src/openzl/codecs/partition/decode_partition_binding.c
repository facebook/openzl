// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/partition/decode_partition_binding.h"

#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/codecs/partition/common_partition.h"
#include "openzl/codecs/partition/decode_partition_kernel.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_dtransform.h"

/// Parse partition parameters from the codec header.
static ZL_Report ZL_PartitionParams_readHeader(
        ZL_PartitionParams* params,
        size_t* width,
        ZL_Decoder* decoder)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(decoder);

    ZL_RBuffer const header = ZL_Decoder_getCodecHeader(decoder);
    ZL_ERR_IF_LT(header.size, 1, corruption, "Empty header");
    const uint8_t* hdr       = (uint8_t const*)header.start;
    const uint8_t* const end = hdr + header.size;
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
        ZL_ERR_IF_EQ(numBits, 0, corruption, "Invalid numBits");
        ZL_ERR_IF_EQ(hdr, end, corruption, "Missing partition sizes");
        ZL_ERR_IF_EQ(
                end[-1], 0, corruption, "Corrupted partition sizes bitstream");
        // The bitstream ends with a 1 bit, so we know when to stop.
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
        uint64_t* partitionSizes = ZL_Decoder_getScratchSpace(
                decoder,
                sizeof(params->partitionSizes[0]) * params->numPartitions);
        ZL_ERR_IF_NULL(partitionSizes, allocation);

        ZS_BitDStreamFF bitstream =
                ZS_BitDStreamFF_init(hdr, (size_t)(end - hdr));
        for (size_t i = 0; i < params->numPartitions; ++i) {
            uint64_t const log2Size = ZS_BitDStreamFF_read(&bitstream, numBits);
            partitionSizes[i]       = 1ULL << log2Size;
            ZS_BitDStreamFF_reload(&bitstream);
        }

        ZL_ERR_IF_ERR(ZS_BitDStreamFF_finish(&bitstream));
        params->partitionSizes = partitionSizes;
    } else {
        const size_t partitionBound =
                ZL_MIN((size_t)(end - hdr), ZL_PARTITION_MAX_PARTITIONS);
        uint64_t* partitionSizes = ZL_Decoder_getScratchSpace(
                decoder, sizeof(params->partitionSizes[0]) * partitionBound);
        ZL_ERR_IF_NULL(partitionSizes, allocation);
        params->numPartitions = 0;
        while (hdr < end) {
            ZL_ERR_IF_GE(
                    params->numPartitions,
                    ZL_PARTITION_MAX_PARTITIONS,
                    corruption);
            ZL_TRY_SET(
                    uint64_t,
                    partitionSizes[params->numPartitions++],
                    ZL_varintDecode(&hdr, end));
        }
        params->partitionSizes = partitionSizes;
    }
    ZL_ERR_IF_NOT(ZL_PartitionParams_validate(params), corruption);
    return ZL_returnSuccess();
}

ZL_Report DI_partition(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_Input const* const bucketsIn = ins[0];
    ZL_Input const* const bitsIn    = ins[1];

    ZL_ERR_IF_NE(ZL_Input_eltWidth(bucketsIn), 1, corruption, "Unsupported");
    ZL_ASSERT_EQ(ZL_Input_type(bitsIn), ZL_Type_serial);

    size_t const numElts = ZL_Input_numElts(bucketsIn);

    ZL_PartitionParams params = { 0 };
    size_t eltWidth           = 0;
    ZL_ERR_IF_ERR(ZL_PartitionParams_readHeader(&params, &eltWidth, dictx));

    ZL_Output* const out =
            ZL_Decoder_create1OutStream(dictx, numElts, eltWidth);
    ZL_ERR_IF_NULL(out, allocation);

    ZL_Report const ret = ZL_partitionDecode(
            ZL_Output_ptr(out),
            eltWidth,
            (uint8_t const*)ZL_Input_ptr(bucketsIn),
            numElts,
            (uint8_t const*)ZL_Input_ptr(bitsIn),
            ZL_Input_numElts(bitsIn),
            &params);
    if (ZL_isError(ret)) {
        return ret;
    }

    ZL_ERR_IF_ERR(ZL_Output_commit(out, numElts));

    return ZL_returnValue(1);
}
