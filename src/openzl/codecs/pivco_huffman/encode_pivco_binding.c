// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/pivco_huffman/encode_pivco_binding.h"

#define HUF_STATIC_LINKING_ONLY

#include <limits.h>
#include <string.h>

#include "openzl/codecs/pivco_huffman/common_pivco_kernel.h"
#include "openzl/codecs/pivco_huffman/encode_pivco_kernel.h"
#include "openzl/fse/huf.h"
#include "openzl/shared/histogram.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"

static ZL_Report buildWeights(
        ZL_Encoder* eictx,
        uint8_t weights[ZL_PIVCO_MAX_SYMBOLS],
        size_t* weightsSize,
        const uint8_t* src,
        size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    ZL_ERR_IF_NULL(weights, GENERIC);
    ZL_ERR_IF_NULL(weightsSize, GENERIC);
    *weightsSize = 0;
    memset(weights, 0, ZL_PIVCO_MAX_SYMBOLS);

    if (srcSize == 0) {
        return ZL_returnValue(0);
    }
    ZL_ERR_IF_NULL(src, node_invalid_input);
    ZL_ERR_IF_GT(
            srcSize,
            UINT_MAX,
            node_invalid_input,
            "PivCo-Huffman input is too large to histogram");

    ZL_Histogram8 hist;
    ZL_Histogram_init(&hist.base, 255);
    ZL_Histogram_build(&hist.base, src, srcSize, 1);

    const uint32_t maxSymbol    = hist.base.maxSymbol;
    const size_t cardinality    = hist.base.cardinality;
    const uint32_t* const count = hist.base.count;

    *weightsSize = (size_t)maxSymbol + 1;
    if (cardinality == 1) {
        weights[maxSymbol] = 1;
        return ZL_returnValue(0);
    }

    HUF_CREATE_STATIC_CTABLE(ctable, HUF_SYMBOLVALUE_MAX);
    unsigned tableLog =
            HUF_optimalTableLog(ZL_PIVCO_MAX_TABLE_LOG, srcSize, maxSymbol);
    const size_t hufRet = HUF_buildCTable(ctable, count, maxSymbol, tableLog);
    ZL_ERR_IF(
            HUF_isError(hufRet),
            GENERIC,
            "HUF_buildCTable failed: %s",
            HUF_getErrorName(hufRet));
    tableLog = (unsigned)hufRet;
    ZL_ERR_IF_GT(tableLog, ZL_PIVCO_MAX_TABLE_LOG, GENERIC);

    for (unsigned symbol = 0; symbol <= maxSymbol; ++symbol) {
        const unsigned numBits = HUF_getNbBitsFromCTable(ctable, symbol);
        const uint8_t weight =
                numBits == 0 ? 0 : (uint8_t)(tableLog + 1 - numBits);
        weights[symbol] = weight;
    }

    return ZL_returnValue(tableLog);
}

/**
 * Writes the codec header: the decoded size, followed -- only when the input
 * spans more than one block -- by the block size. A single-block input omits
 * the block size; the decoder then defaults it to the decoded size.
 */
static size_t
encodeHeader(uint8_t* header, size_t decodedSize, size_t blockSize)
{
    uint8_t* ptr = header;
    ptr += ZL_varintEncode((uint64_t)decodedSize, ptr);
    if (blockSize < decodedSize) {
        ptr += ZL_varintEncode((uint64_t)blockSize, ptr);
    }
    return (size_t)(ptr - header);
}

ZL_Report
EI_pivco_huffman(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);

    ZL_ASSERT_EQ(nbIns, 1);
    const ZL_Input* const in = ins[0];
    ZL_ASSERT_EQ(ZL_Input_type(in), ZL_Type_serial);

    const uint8_t* const src = ZL_Input_ptr(in);
    size_t const srcSize     = ZL_Input_numElts(in);

    uint8_t weights[ZL_PIVCO_MAX_SYMBOLS];
    size_t weightsSize;
    ZL_TRY_LET_CONST(
            size_t,
            tableLog,
            buildWeights(eictx, weights, &weightsSize, src, srcSize));

    // In the future the block size could be configurable.
    size_t const blockSize = ZL_PIVCO_DEFAULT_BLOCK_SIZE;

    // Header is the decoded size plus an optional block size (see
    // encodeHeader).
    enum { kHeaderCapacity = 2 * ZL_VARINT_LENGTH_64 };
    uint8_t header[kHeaderCapacity];
    size_t const headerSize = encodeHeader(header, srcSize, blockSize);
    ZL_Encoder_sendCodecHeader(eictx, header, headerSize);

    ZL_Output* const weightsStream =
            ZL_Encoder_createTypedStream(eictx, 0, weightsSize, 1);
    ZL_ERR_IF_NULL(weightsStream, allocation);
    if (weightsSize != 0) {
        memcpy(ZL_Output_ptr(weightsStream), weights, weightsSize);
    }
    ZL_ERR_IF_ERR(ZL_Output_commit(weightsStream, weightsSize));

    const size_t dstCapacity = ZL_PivCoHuffmanEncode_bound(srcSize, blockSize);
    ZL_Output* const bitstream =
            ZL_Encoder_createTypedStream(eictx, 1, dstCapacity, 1);
    ZL_ERR_IF_NULL(bitstream, allocation);

    const size_t scratchElements =
            ZL_PivCoHuffmanEncode_scratchElements(srcSize, blockSize);
    uint8_t* const scratch = ZL_Encoder_getScratchSpace(eictx, scratchElements);
    ZL_ERR_IF_NULL(scratch, allocation);

    const size_t dstSize = ZL_PivCoHuffman_encode(
            ZL_Output_ptr(bitstream),
            dstCapacity,
            scratch,
            scratchElements,
            weights,
            weightsSize,
            (int)tableLog,
            src,
            srcSize,
            blockSize,
            NULL);
    ZL_ERR_IF_EQ(dstSize, SIZE_MAX, GENERIC, "PivCo-Huffman encoding failed");
    ZL_ERR_IF_ERR(ZL_Output_commit(bitstream, dstSize));

    return ZL_returnSuccess();
}
