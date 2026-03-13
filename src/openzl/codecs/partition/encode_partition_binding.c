// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/partition/encode_partition_binding.h"

#include "openzl/codecs/common/bitstream/ff_bitstream.h"
#include "openzl/codecs/partition/common_partition.h"
#include "openzl/codecs/partition/encode_partition_kernel.h"
#include "openzl/codecs/zl_partition.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/numeric_operations.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_ctransform.h"

ZL_Report ZL_PartitionParams_fromLocalParams(
        ZL_PartitionParams* params,
        const ZL_LocalParams* localParams)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);

    for (size_t i = 0; i < localParams->intParams.nbIntParams; ++i) {
        if (localParams->intParams.intParams[i].paramId
            == ZL_PARTITION_PRESET_PID) {
            const ZL_PartitionParamsPreset preset =
                    (ZL_PartitionParamsPreset)localParams->intParams
                            .intParams[i]
                            .paramValue;
            const ZL_PartitionParams* presetParams =
                    ZL_PartitionParams_getPreset(preset);
            if (presetParams != NULL) {
                *params = *presetParams;
                ZL_ASSERT(ZL_PartitionParams_validate(params));
                return ZL_returnValue(preset);
            }
            break;
        }
    }

    for (size_t i = 0; i < localParams->copyParams.nbCopyParams; ++i) {
        if (localParams->copyParams.copyParams[i].paramId
            == ZL_PARTITION_CUSTOM_PID) {
            const ZL_CopyParam param = localParams->copyParams.copyParams[i];
            ZL_ERR_IF_NE(
                    param.paramSize % sizeof(uint64_t),
                    0,
                    nodeParameter_invalid);
            ZL_ERR_IF_LT(
                    param.paramSize,
                    2 * sizeof(uint64_t),
                    nodeParameter_invalid);
            const uint64_t* partitionsPtr = (const uint64_t*)param.paramPtr;
            params->startValue            = *partitionsPtr;
            params->partitionSizes        = partitionsPtr + 1;
            params->numPartitions = param.paramSize / sizeof(uint64_t) - 1;
            ZL_ERR_IF_NOT(
                    ZL_PartitionParams_validate(params), nodeParameter_invalid);
            return ZL_returnValue(ZL_PartitionParamsPreset_custom);
        }
    }
    ZL_ERR(nodeParameter_invalid, "Neither preset nor custom partition found");
}

/// Write partition parameters into the codec header.
static ZL_Report ZL_PartitionParams_sendHeader(
        const ZL_PartitionParams* params,
        const uint8_t* bits,
        ZL_PartitionParamsPreset preset,
        size_t eltWidth,
        ZL_Encoder* encoder)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(encoder);
    uint8_t flags = (uint8_t)ZL_nextPow2(eltWidth);
    if (preset != ZL_PartitionParamsPreset_custom) {
        ZL_STATIC_ASSERT(
                ZL_PartitionParamsPreset_custom <= 32, "must fit in 5 bits");
        flags |= ZL_PARTITION_HEADER_IS_PRESET_BIT;
        flags |= (uint8_t)(preset << 3);
        ZL_Encoder_sendCodecHeader(encoder, &flags, 1);
        return ZL_returnSuccess();
    }

    if (params->startValue == 0) {
        flags |= ZL_PARTITION_HEADER_IS_FIRST_VALUE_ZERO_BIT;
    }

    // TODO: Consider a RLE-like scheme. E.g. if the value shows up twice in a
    // row, the next value is a repeat count rather than a new value.
    if (ZL_PartitionParams_areAllSizesPow2(params)) {
        flags |= ZL_PARTITION_HEADER_IS_POW2_BIT;

        size_t numBits = (size_t)ZL_nextPow2(
                NUMOP_findMaxU8(bits, params->numPartitions));
        numBits = ZL_MAX(numBits, 3);
        ZL_ASSERT_LE(numBits, 6, "Impossible for valid params");

        flags |= (uint8_t)((numBits - 3) << 6);

        const size_t headerSizeBound = 1 + ZL_VARINT_LENGTH_64
                + (numBits * params->numPartitions + 1 + 7) / 8;
        uint8_t* header = ZL_Encoder_getScratchSpace(encoder, headerSizeBound);
        ZL_ERR_IF_NULL(header, allocation);
        size_t size    = 0;
        header[size++] = flags;
        if (params->startValue != 0) {
            size += ZL_varintEncode(params->startValue, header + size);
        }
        ZS_BitCStreamFF bitstream =
                ZS_BitCStreamFF_init(header + size, headerSizeBound - size);

        for (size_t i = 0; i < params->numPartitions; ++i) {
            ZS_BitCStreamFF_write(&bitstream, bits[i], numBits);
            ZS_BitCStreamFF_flush(&bitstream);
        }
        ZS_BitCStreamFF_write(&bitstream, 1, 1);
        ZL_TRY_LET(size_t, bitSize, ZS_BitCStreamFF_finish(&bitstream));
        size += bitSize;

        ZL_Encoder_sendCodecHeader(encoder, header, size);
        return ZL_returnSuccess();
    } else {
        const size_t headerSizeBound =
                1 + ZL_VARINT_LENGTH_64 * (params->numPartitions + 1);
        uint8_t* header = ZL_Encoder_getScratchSpace(encoder, headerSizeBound);
        ZL_ERR_IF_NULL(header, allocation);
        size_t size    = 0;
        header[size++] = flags;
        if (params->startValue != 0) {
            size += ZL_varintEncode(params->startValue, header + size);
        }
        for (size_t i = 0; i < params->numPartitions; ++i) {
            size += ZL_varintEncode(params->partitionSizes[i], header + size);
        }
        ZL_Encoder_sendCodecHeader(encoder, header, size);
        return ZL_returnSuccess();
    }
}

ZL_Report EI_partitionWithParams(
        ZL_Encoder* eictx,
        const ZL_Input* ins[],
        size_t nbIns,
        const ZL_PartitionParams* params,
        ZL_PartitionParamsPreset preset)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ASSERT_EQ(nbIns, 1);
    ZL_ASSERT_NN(ins);
    const ZL_Input* in = ins[0];
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_numeric);
    size_t const eltWidth = ZL_Input_eltWidth(in);
    size_t const numElts  = ZL_Input_numElts(in);

    uint8_t* bits = ZL_Encoder_getScratchSpace(eictx, params->numPartitions);
    ZL_ERR_IF_NULL(bits, allocation);
    ZL_PartitionParams_computeBits(params, bits);
    const size_t maxNumBits = NUMOP_findMaxU8(bits, params->numPartitions);

    ZL_Output* bucketOut = ZL_Encoder_createTypedStream(eictx, 0, numElts, 1);
    ZL_ERR_IF_NULL(bucketOut, allocation);

    size_t const bitsCapacity = (maxNumBits * numElts + 7) / 8;
    ZL_Output* bitsOut =
            ZL_Encoder_createTypedStream(eictx, 1, bitsCapacity, 1);
    ZL_ERR_IF_NULL(bitsOut, allocation);

    ZL_TRY_LET(
            size_t,
            bitsSize,
            ZL_partitionEncode(
                    (uint8_t*)ZL_Output_ptr(bitsOut),
                    bitsCapacity,
                    (uint8_t*)ZL_Output_ptr(bucketOut),
                    ZL_Input_ptr(in),
                    numElts,
                    eltWidth,
                    params));
    ZL_ASSERT_LE(bitsSize, bitsCapacity);

    ZL_ERR_IF_ERR(ZL_PartitionParams_sendHeader(
            params, bits, preset, eltWidth, eictx));
    ZL_ERR_IF_ERR(ZL_Output_commit(bucketOut, numElts));
    ZL_ERR_IF_ERR(ZL_Output_commit(bitsOut, bitsSize));

    return ZL_returnSuccess();
}

ZL_Report EI_partition(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_PartitionParams params;
    ZL_TRY_LET(
            size_t,
            preset,
            ZL_PartitionParams_fromLocalParams(
                    &params, ZL_Encoder_getLocalParams(eictx)));
    return EI_partitionWithParams(
            eictx, ins, nbIns, &params, (ZL_PartitionParamsPreset)preset);
}

ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_buildPartitionNodeFromPreset(
        ZL_Compressor* compressor,
        ZL_PartitionParamsPreset preset)
{
    ZL_IntParam param        = { ZL_PARTITION_PRESET_PID, (int)preset };
    ZL_LocalParams lp        = { .intParams = { &param, 1 } };
    ZL_NodeParameters params = { .localParams = &lp };
    return ZL_Compressor_parameterizeNode(
            compressor, ZL_NODE_PARTITION, &params);
}

ZL_RESULT_OF(ZL_NodeID)
ZL_Compressor_buildPartitionNode(
        ZL_Compressor* compressor,
        uint64_t startValue,
        const uint64_t* partitionSizes,
        size_t numPartitions)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_NodeID, compressor);

    ZL_ERR_IF_GT(
            numPartitions,
            ZL_PARTITION_MAX_PARTITIONS,
            nodeParameter_invalid,
            "Partition codec only supports up to 256 partitions");

    uint64_t array[ZL_PARTITION_MAX_PARTITIONS + 1];
    array[0] = startValue;
    memcpy(&array[1], partitionSizes, sizeof(uint64_t) * numPartitions);
    ZL_CopyParam param       = { ZL_PARTITION_CUSTOM_PID,
                                 array,
                                 sizeof(uint64_t) * (numPartitions + 1) };
    ZL_LocalParams lp        = { .copyParams = { &param, 1 } };
    ZL_NodeParameters params = { .localParams = &lp };
    return ZL_Compressor_parameterizeNode(
            compressor, ZL_NODE_PARTITION, &params);
}
