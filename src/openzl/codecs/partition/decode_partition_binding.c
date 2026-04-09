// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "openzl/codecs/partition/decode_partition_binding.h"

#include "openzl/codecs/partition/common_partition.h"
#include "openzl/codecs/partition/decode_partition_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/zl_dtransform.h"

/// Parse partition parameters from the codec header.
static ZL_Report ZL_PartitionParams_readHeader(
        ZL_PartitionParams* params,
        size_t* width,
        ZL_Decoder* decoder)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(decoder);

    ZL_RBuffer const header = ZL_Decoder_getCodecHeader(decoder);

    uint64_t* partitionSizesBuffer = ZL_Decoder_getScratchSpace(
            decoder, sizeof(uint64_t) * ZL_PARTITION_MAX_PARTITIONS);
    ZL_ERR_IF_NULL(partitionSizesBuffer, allocation);

    ZL_ERR_IF_ERR(ZL_PartitionParams_parseHeader(
            params,
            width,
            (const uint8_t*)header.start,
            header.size,
            partitionSizesBuffer));

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
