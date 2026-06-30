// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/pivco_huffman/decode_pivco_binding.h"

#include <stdint.h>

#include "openzl/codecs/pivco_huffman/common_pivco_kernel.h"
#include "openzl/codecs/pivco_huffman/decode_pivco_kernel.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"

typedef struct {
    size_t decodedSize;
    size_t blockSize;
} PivCoHuffmanHeader;

static ZL_Report decodeHeader(ZL_Decoder* dictx, PivCoHuffmanHeader* parsed)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ERR_IF_NULL(parsed, GENERIC);

    const ZL_RBuffer header = ZL_Decoder_getCodecHeader(dictx);
    ZL_ERR_IF_EQ(header.size, 0, corruption);
    ZL_ERR_IF_NULL(header.start, corruption);
    const uint8_t* ptr       = (const uint8_t*)header.start;
    const uint8_t* const end = ptr + header.size;

    ZL_TRY_LET_CONST(uint64_t, decodedSize64, ZL_varintDecode(&ptr, end));
    ZL_ERR_IF_GT(decodedSize64, (uint64_t)SIZE_MAX, corruption);
    parsed->decodedSize = (size_t)decodedSize64;

    // The block size is optional: when present it follows the decoded size,
    // otherwise it defaults to the decoded size (a single block).
    if (ptr != end) {
        ZL_TRY_LET_CONST(uint64_t, blockSize64, ZL_varintDecode(&ptr, end));
        ZL_ERR_IF_GT(blockSize64, (uint64_t)SIZE_MAX, corruption);
        ZL_ERR_IF_EQ(blockSize64, 0, corruption);
        parsed->blockSize = (size_t)blockSize64;
    } else {
        parsed->blockSize = parsed->decodedSize;
    }
    // Explicitly ignore unconsumed bytes in the header.
    // This allows the encoder to add extra information in the future without
    // breaking backwards compatibility.
    // For example: The encoder could add a jump table to the offset of each
    // encoded block for parallel decoding.

    return ZL_returnSuccess();
}

ZL_Report DI_pivco_huffman(ZL_Decoder* dictx, const ZL_Input* ins[])
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);

    const ZL_Input* const weightsStream = ins[0];
    const ZL_Input* const bitstream     = ins[1];

    ZL_ASSERT_EQ(ZL_Input_type(weightsStream), ZL_Type_numeric);
    ZL_ERR_IF_NE(ZL_Input_eltWidth(weightsStream), 1, corruption);

    ZL_ASSERT_EQ(ZL_Input_type(bitstream), ZL_Type_serial);
    ZL_ASSERT_EQ(ZL_Input_eltWidth(bitstream), 1);

    PivCoHuffmanHeader header = { 0, 0 };
    ZL_ERR_IF_ERR(decodeHeader(dictx, &header));

    const size_t weightsSize   = ZL_Input_numElts(weightsStream);
    const size_t bitstreamSize = ZL_Input_numElts(bitstream);
    ZL_ERR_IF_GT(weightsSize, ZL_PIVCO_MAX_SYMBOLS, corruption);
    ZL_ERR_IF_NE(header.decodedSize == 0, weightsSize == 0, corruption);

    ZL_Output* const out =
            ZL_Decoder_create1OutStream(dictx, header.decodedSize, 1);
    ZL_ERR_IF_NULL(out, allocation);

    const size_t scratchBytes = ZL_PivCoHuffmanDecode_scratchBytes(
            header.decodedSize, header.blockSize);
    uint8_t* const scratch = ZL_Decoder_getScratchSpace(dictx, scratchBytes);
    ZL_ERR_IF_NULL(scratch, allocation);

    const uint8_t* const weights = ZL_Input_ptr(weightsStream);
    ZL_ERR_IF_NULL(weights, corruption);
    ZL_ERR_IF_NOT(
            ZL_PivCoHuffman_decode(
                    ZL_Output_ptr(out),
                    header.decodedSize,
                    scratch,
                    scratchBytes,
                    weights,
                    weightsSize,
                    ZL_Input_ptr(bitstream),
                    bitstreamSize,
                    header.blockSize,
                    NULL),
            corruption,
            "PivCo-Huffman decode failed");
    ZL_ERR_IF_ERR(ZL_Output_commit(out, header.decodedSize));

    return ZL_returnSuccess();
}
