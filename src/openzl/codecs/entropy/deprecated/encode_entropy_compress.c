// Copyright (c) Meta Platforms, Inc. and affiliates.

#define FSE_STATIC_LINKING_ONLY
#define HUF_STATIC_LINKING_ONLY

#include "openzl/codecs/bitpack/common_bitpack_kernel.h"
#include "openzl/codecs/conversion/common_endianness_kernel.h"
#include "openzl/codecs/entropy/deprecated/common_entropy.h"
#include "openzl/codecs/entropy/deprecated/common_huf_avx2.h"
#include "openzl/codecs/entropy/encode_huffman_kernel.h"
#include "openzl/common/assertion.h"
#include "openzl/common/speed.h"
#include "openzl/fse/fse.h"
#include "openzl/fse/huf.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/histogram.h"
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_errors.h"

#define ZS_ENTROPY_MULTI_THRESHOLD 100000
#define ZS_ENTROPY_FSE_SHARE_THRESHOLD 800
#define ZS_ENTROPY_HUF_SHARE_THRESHOLD 12800
#define ZS_ENTROPY_BLOCK_SPLIT_FIXED_SIZE (1 << 15)
#define ZS_ENTROPY_HUF_AVX2_THRESHOLD 10000

ZS_Entropy_EncodeParameters ZS_Entropy_EncodeParameters_fromAllowedTypes(
        ZS_Entropy_TypeMask_e allowedTypes)
{
    ZS_Entropy_EncodeParameters params = {
        .allowedTypes = allowedTypes,
        .encodeSpeed =
                ZL_EncodeSpeed_fromBaseline(ZL_EncodeSpeedBaseline_entropy),
        .decodeSpeed = ZL_DecodeSpeed_fromBaseline(ZL_DecodeSpeedBaseline_any),
        .precomputedHistogram = NULL,
        .cardinalityEstimate  = 0,
        .maxValueUpperBound   = 0,
        .maxTableLog          = 0,
        .allowAvx2Huffman     = ZS_ENTROPY_AVX2_HUFFMAN_DEFAULT,
        .blockSplits          = NULL,
        .tableManager         = NULL,
        .fseNbStates          = ZS_ENTROPY_DEFAULT_FSE_NBSTATES,
    };
    return params;
}

ZL_Report ZS_Entropy_encodeHuf(
        ZL_WC* dst,
        void const* src,
        size_t srcSize,
        size_t elementSize)
{
    ZS_Entropy_EncodeParameters const params =
            ZS_Entropy_EncodeParameters_fromAllowedTypes(
                    ZS_Entropy_TypeMask_huf | ZS_Entropy_TypeMask_raw
                    | ZS_Entropy_TypeMask_constant | ZS_Entropy_TypeMask_multi
                    | ZS_Entropy_TypeMask_bit);
    return ZS_Entropy_encode(dst, src, srcSize, elementSize, &params);
}

ZL_Report ZS_Entropy_encodeFse(
        ZL_WC* dst,
        void const* src,
        size_t srcSize,
        size_t elementSize,
        uint8_t nbStates)
{
    ZS_Entropy_EncodeParameters params =
            ZS_Entropy_EncodeParameters_fromAllowedTypes(
                    ZS_Entropy_TypeMask_fse | ZS_Entropy_TypeMask_raw
                    | ZS_Entropy_TypeMask_constant | ZS_Entropy_TypeMask_multi
                    | ZS_Entropy_TypeMask_bit);
    if (nbStates) {
        params.fseNbStates = nbStates;
    }
    return ZS_Entropy_encode(dst, src, srcSize, elementSize, &params);
}

size_t ZS_Entropy_encodedSizeBound(size_t srcSize, size_t elementSize)
{
    size_t const varintSize = srcSize > 0xF ? ZL_varintSize(srcSize >> 4) : 0;
    // TODO: Remove the extra from the LA Huf
    size_t const extra = elementSize == 2 ? 1 + 2 * ZL_varintSize(srcSize) : 0;
    size_t const encodeBound = 1 + varintSize + (srcSize * elementSize) + extra;
    ZL_DLOG(V7, "bound = %zu | %zu | %zu", encodeBound, srcSize, elementSize);
    return encodeBound;
}

static ZL_Report ZS_Entropy_encodeBit(
        ZL_WC* dst,
        void const* src,
        size_t srcSize,
        size_t elementSize,
        uint64_t maxSymbolValue,
        ZS_Entropy_EncodeParameters const* params);

static size_t ZS_Entropy_entropySizeBound(
        size_t srcSize,
        size_t elementSize,
        unsigned maxSymbol,
        ZS_Entropy_TypeMask_e allowedTypes)
{
    ZL_ASSERT_LE(elementSize, 4);
    size_t const nbBits =
            1 + (size_t)(maxSymbol == 0 ? 0 : ZL_highbit32(maxSymbol));
    size_t const bitSize = (nbBits * srcSize + 7) / 8;
    if (allowedTypes & ZS_Entropy_TypeMask_bit) {
        return bitSize;
    }
    return srcSize * elementSize;
}

static bool ZS_Entropy_useConstant(
        void const* src,
        size_t srcSize,
        size_t elementSize,
        ZS_Entropy_EncodeParameters const* params)
{
    if (elementSize < 1 || elementSize > 8 || !ZL_isPow2(elementSize)) {
        return false;
    }
    if (srcSize <= 1 && (params->allowedTypes & ZS_Entropy_TypeMask_raw)) {
        return false;
    }
    if (!(params->allowedTypes & ZS_Entropy_TypeMask_constant)) {
        return false;
    }
    // TODO: This could be vectorized...
    switch (elementSize) {
        case 1: {
            uint8_t const* src8 = (uint8_t const*)src;
            uint8_t const value = src8[0];
            for (size_t i = 1; i < srcSize; ++i) {
                if (src8[i] != value) {
                    return false;
                }
            }
            return true;
        }
        case 2: {
            uint16_t const* src16 = (uint16_t const*)src;
            uint16_t const value  = ZL_read16(src16);
            for (size_t i = 1; i < srcSize; ++i) {
                if (ZL_read16(&src16[i]) != value) {
                    return false;
                }
            }
            return true;
        }
        case 4: {
            uint32_t const* src32 = (uint32_t const*)src;
            uint32_t const value  = src32[0];
            for (size_t i = 1; i < srcSize; ++i) {
                if (src32[i] != value) {
                    return false;
                }
            }
            return true;
        }
        case 8: {
            uint64_t const* src64 = (uint64_t const*)src;
            uint64_t const value  = src64[0];
            for (size_t i = 1; i < srcSize; ++i) {
                if (src64[i] != value) {
                    return false;
                }
            }
            return true;
        }
        default:
            ZL_ASSERT_FAIL("Not reachable");
            return false;
    }
}

static ZL_Report ZS_RawAndConstant_writeHeader(
        ZL_WC* dst,
        size_t srcSize,
        ZS_Entropy_Type_e type)
{
    ZL_ASSERT_LT(type, 0x8);
    int const needVarint = srcSize > 0xF ? 1 : 0;
    int const hdr        = (int)type      /* type: bits [0, 3) */
            | (((int)srcSize & 0xF) << 3) /* decoded size: bits [0, 7) */
            | (needVarint << 7) /* more varint: bits [7, 8) */;
    size_t const varintSize = needVarint ? ZL_varintSize(srcSize >> 4) : 0;
    size_t const hdrSize    = 1 + varintSize;
    if (ZL_WC_avail(dst) < hdrSize) {
        ZL_RET_R_ERR(GENERIC);
    }
    ZL_WC_push(dst, (uint8_t)hdr);
    if (needVarint) {
        ZL_WC_pushVarint(dst, srcSize >> 4);
    }
    ZL_DLOG(V7,
            "Decoded size = %llu (headerSize = %u)",
            (unsigned long long)srcSize,
            (unsigned)hdrSize);
    return ZL_returnSuccess();
}

/// Encoding using RAW or Constant only
static ZL_Report ZS_Entropy_encodeFastest(
        ZL_WC* dst,
        void const* src,
        size_t srcSize,
        size_t elementSize,
        ZS_Entropy_EncodeParameters const* params)
{
    ZL_DLOG(V7, "Fastest");
    // TODO: Could enable block splitting for LZ4 decoding speeds (not fastest)
    if (ZS_Entropy_useConstant(src, srcSize, elementSize, params)) {
        ZL_RET_R_IF_ERR(ZS_RawAndConstant_writeHeader(
                dst, srcSize, ZS_Entropy_Type_constant));
        return ZS_Constant_encode(dst, src, elementSize);
    }
    ZL_DLOG(V7, "dst avail = %zu", ZL_WC_avail(dst));
    if (params->allowedTypes & ZS_Entropy_TypeMask_raw) {
        ZL_RET_R_IF_ERR(ZS_RawAndConstant_writeHeader(
                dst, srcSize, ZS_Entropy_Type_raw));
        return ZS_Raw_encode(dst, src, srcSize, elementSize);
    }
    ZL_RET_R_ERR(GENERIC);
}

static size_t ZS_HufAndFse_headerSize(size_t srcSize, size_t maxDstSize)
{
    int const needVarint = srcSize > 0x1F || maxDstSize > 0x0F;
    if (!needVarint) {
        return 2;
    }
    return 2 + ZL_varintSize(srcSize >> 5) + ZL_varintSize(maxDstSize >> 4);
}

static ZL_Report ZS_HufAndFse_writeHeader(
        uint8_t* header,
        size_t headerSize,
        size_t srcSize,
        size_t dstSize,
        int tableMode,
        bool format,
        ZS_Entropy_Type_e type)
{
    ZL_ASSERT_LE(ZS_HufAndFse_headerSize(srcSize, dstSize), headerSize);
    uint8_t* const headerEnd = header + headerSize;
    int const needVarint     = headerSize > 2 ? 1 : 0;
    int const hdr            = (int)type   /* type: bits [0, 3) */
            | (tableMode << 3)             /* table-mode: bits [3, 5) */
            | (format << 5)                /* format: bits [5, 6) */
            | (needVarint << 6)            /* need-varint: bits [6, 7) */
            | (int)((srcSize & 0x1F) << 7) /* decoded-size: bits [7, 12) */
            | (int)((dstSize & 0x0F) << 12) /* encoded-size: bits [12, 16) */;
    // Set the varint high bits, even if we won't write them.
    memset(header, 0x80, headerSize);
    ZL_writeLE16(header, (uint16_t)hdr);
    header += 2;
    if (needVarint) {
        header += ZL_varintEncode((uint64_t)srcSize >> 5, header);
        header += ZL_varintEncode(dstSize >> 4, header);
        ZL_ASSERT_EQ(header[-1] & 0x80, 0);
        if (header < headerEnd) {
            header[-1] |= 0x80;
            headerEnd[-1] = 0x00;
        }
        header = headerEnd;
    }
    ZL_DLOG(V7,
            "type = %d | tableMode = %d | encodedSize = %llu | decodedSize = %llu (headerSize = %zu)",
            (int)type,
            tableMode,
            (unsigned long long)dstSize,
            (unsigned long long)srcSize,
            headerSize);
    ZL_ASSERT_EQ(header, headerEnd);

    return ZL_returnSuccess();
}

static ZL_Report ZS_Entropy_encodeLAHuf(
        ZL_WC* dst,
        void const* src,
        size_t srcSize,
        size_t elementSize,
        ZS_Entropy_EncodeParameters const* params)
{
    ZL_DLOG(V7, "LA Huf (srcSize = %zu)", srcSize);
    uint16_t const* src16 = (uint16_t const*)src;
    ZL_ASSERT_EQ(elementSize, 2);
    if (!ZL_uintFits(params->maxValueUpperBound, sizeof(uint16_t))) {
        ZL_RET_R_ERR(GENERIC);
    }
    uint16_t maxSymbolValue = (uint16_t)params->maxValueUpperBound;
    size_t maxSymbolCount   = 0;
    if (maxSymbolValue == 0) {
        // TODO: Optimize
        for (size_t i = 0; i < srcSize; ++i) {
            const uint16_t value = ZL_read16(&src16[i]);
            if (value > maxSymbolValue) {
                maxSymbolValue = value;
                maxSymbolCount = 0;
            } else if (value == maxSymbolValue) {
                ++maxSymbolCount;
            }
        }
    }
    // TODO(terrelln): We should to check the histogram first to see if
    // Huffman actually beats bitpacking or raw or constant.
    if (params->allowedTypes & ZS_Entropy_TypeMask_huf) {
        bool const useAvx2 = params->allowAvx2Huffman && maxSymbolValue <= 1024
                && srcSize >= ZS_ENTROPY_HUF_AVX2_THRESHOLD;
        ZL_WC dst2              = *dst;
        size_t const maxDstSize = ZS_Entropy_entropySizeBound(
                srcSize, elementSize, maxSymbolValue, params->allowedTypes);
        size_t const headerSize = ZS_HufAndFse_headerSize(srcSize, maxDstSize);
        if (ZL_WC_avail(&dst2) < headerSize) {
            ZL_RET_R_ERR(GENERIC);
        }
        uint8_t* const header = ZL_WC_ptr(&dst2);
        ZL_WC_advance(&dst2, headerSize);
        bool error = false;
        if (useAvx2) {
            size_t const csize = ZS_Huf16Avx2_encode(
                    ZL_WC_ptr(&dst2), ZL_WC_avail(&dst2), src, srcSize);
            if (csize == 0) {
                error = true;
            }
            ZL_WC_advance(&dst2, csize);
        } else {
            ZL_Report const ret = ZS_largeHuffmanEncode(
                    &dst2,
                    src,
                    srcSize,
                    maxSymbolValue,
                    (int)params->maxTableLog);
            if (ZL_isError(ret)) {
                error = true;
            }
        }
        size_t const totalCSize = ZL_WC_avail(dst) - ZL_WC_avail(&dst2);
        size_t const hufCSize   = totalCSize - headerSize;
        if (!error && totalCSize < maxDstSize) {
            *dst = dst2;
            ZL_RET_R_IF_ERR(ZS_HufAndFse_writeHeader(
                    header,
                    headerSize,
                    srcSize,
                    hufCSize,
                    0 /* TODO: Large alphabet Huffman currently ignores the
                         table mode */
                    ,
                    useAvx2,
                    ZS_Entropy_Type_huf));
            return ZL_returnSuccess();
        }
    }
    if (maxSymbolValue < (1 << 15) && maxSymbolCount < srcSize
        && (params->allowedTypes & ZS_Entropy_TypeMask_bit)) {
        return ZS_Entropy_encodeBit(
                dst, src, srcSize, elementSize, maxSymbolValue, params);
    }
    return ZS_Entropy_encodeFastest(dst, src, srcSize, elementSize, params);
}

static bool ZS_Entropy_useFastest(
        size_t srcSize,
        size_t elementSize,
        ZS_Entropy_EncodeParameters const* params)
{
    int const fastestMask =
            ZS_Entropy_TypeMask_raw | ZS_Entropy_TypeMask_constant;
    // Only RAW supported for srcSize == 0. Choose even if not allowed, it will
    // fail.
    if (srcSize == 0) {
        return true;
    }
    // Only RAW/Constant support elements > 2.
    if (elementSize > 2) {
        return true;
    }
    if (!(params->allowedTypes & fastestMask)) {
        return false;
    }
    if (!(params->allowedTypes & ~fastestMask)) {
        return true;
    }
    if (params->decodeSpeed.baseline >= ZL_DecodeSpeedBaseline_lz4) {
        return true;
    }
    if (params->encodeSpeed.baseline > ZL_EncodeSpeedBaseline_entropy) {
        return true;
    }
    return false;
}

static ZS_Entropy_Type_e ZS_Entropy_selectType(
        ZL_Histogram const* histogram,
        ZS_Entropy_EncodeParameters const* params)
{
    ZL_DLOG(V7, "Selecting type...");
    bool const fseSupported =
            (params->allowedTypes & ZS_Entropy_TypeMask_fse) != 0;
    bool const hufSupported =
            (params->allowedTypes & ZS_Entropy_TypeMask_huf) != 0;
    bool const rleSupported =
            (params->allowedTypes & ZS_Entropy_TypeMask_constant) != 0;
    bool const rawSupported =
            (params->allowedTypes & ZS_Entropy_TypeMask_raw) != 0;
    bool const bitSupported =
            (params->allowedTypes & ZS_Entropy_TypeMask_bit) != 0;

    if (histogram->largestCount == histogram->total && rleSupported) {
        return ZS_Entropy_Type_constant;
    }

    ZL_DLOG(V7,
            "total %u | largest %u",
            histogram->total,
            histogram->largestCount);
    unsigned const maxShare =
            (100 * histogram->total) / histogram->largestCount;

    // TODO: This is a very simplistic selection, and doesn't take decoding
    // speed into account.
    if (bitSupported) {
        size_t const numBits = 1
                + (size_t)(histogram->maxSymbol == 0
                                   ? 0
                                   : ZL_highbit32(
                                             (unsigned)(histogram->maxSymbol)));
        unsigned flatShare = (100u << numBits) - 50;
        if (numBits >= 2) {
            flatShare -= (100u << (numBits - 2));
        }
        ZL_DLOG(V7, "numBits = %zu", numBits);
        ZL_DLOG(V7, "at %d", (int)params->allowedTypes);
        ZL_DLOG(V7, "max share vs flat share: %u : %u", maxShare, flatShare);
        if (numBits <= 2 && maxShare >= flatShare) {
            return ZS_Entropy_Type_bit;
        }
        if (numBits <= 4 && maxShare >= flatShare) {
            return ZS_Entropy_Type_bit;
        }
        if (numBits < 8 && maxShare >= flatShare) {
            return ZS_Entropy_Type_bit;
        }
        // Only bit supported
        if (numBits < 8 && params->allowedTypes == ZS_Entropy_TypeMask_bit) {
            return ZS_Entropy_Type_bit;
        }
    }
    // Has no probabilities >= 1/128 -> Use RAW
    if (maxShare > ZS_ENTROPY_HUF_SHARE_THRESHOLD && rawSupported) {
        return ZS_Entropy_Type_raw;
    }
    // Has at least one very high frequency symbol --> Use FSE
    if (maxShare < ZS_ENTROPY_FSE_SHARE_THRESHOLD && fseSupported) {
        return ZS_Entropy_Type_fse;
    }
    if (hufSupported) {
        return ZS_Entropy_Type_huf;
    }
    if (fseSupported) {
        return ZS_Entropy_Type_fse;
    }
    return ZS_Entropy_Type_raw;
}

static ZL_Report ZS_Multi_writeHeader(ZL_WC* dst, size_t numBlocks)
{
    int const needVarint = numBlocks > 0xF ? 1 : 0;
    int const hdr        = (int)ZS_Entropy_Type_multi /* type: bits [0, 3) */
            | (((int)numBlocks & 0xF) << 3) /* decoded size: bits [0, 7) */
            | (needVarint << 7) /* more varint: bits [7, 8) */;
    size_t const hdrSize = 1 + ZL_varintSize(numBlocks >> 4);
    if (ZL_WC_avail(dst) < hdrSize) {
        ZL_RET_R_ERR(GENERIC);
    }
    ZL_WC_push(dst, (uint8_t)hdr);
    ZL_DLOG(V7, "MULTI HEADER = %zu", numBlocks);
    if (needVarint) {
        ZL_DLOG(V7, "need varint");
        ZL_WC_pushVarint(dst, numBlocks >> 4);
    }
    return ZL_returnSuccess();
}

static ZL_Report ZS_Entropy_encodeBlockSplit(
        ZL_WC* dst,
        void const* src,
        size_t srcSize,
        size_t elementSize,
        ZS_Entropy_EncodeParameters const* params)
{
    ZL_WC dstOriginal = *dst;
    ZL_ASSERT_NN(params->blockSplits);
    ZS_Entropy_EncodeParameters paramsCopy = *params;
    paramsCopy.blockSplits                 = NULL;
    size_t const nbSplits                  = params->blockSplits->nbSplits;
    size_t const nbBlocks                  = nbSplits + 1;
    ZL_DLOG(V7, "SPLIT %zu into %zu blocks", srcSize, nbBlocks);
    ZL_RET_R_IF_ERR(ZS_Multi_writeHeader(dst, nbBlocks));
    for (size_t b = 0; b < nbBlocks; ++b) {
        size_t const begin = b == 0 ? 0 : params->blockSplits->splits[b - 1];
        size_t const end =
                b == nbSplits ? srcSize : params->blockSplits->splits[b];
        ZL_ASSERT_LT(begin, end);

        void const* blockSrc   = (uint8_t const*)src + begin * elementSize;
        size_t const blockSize = (end - begin);

        size_t const avail1 = ZL_WC_avail(dst);
        ZL_DLOG(V7, "Encoding block %zu: [%zu, %zu)...", b, begin, end);
        ZL_Report const ret = ZS_Entropy_encode(
                dst, blockSrc, blockSize, elementSize, &paramsCopy);

        if (ZL_isError(ret)) {
            ZL_DLOG(V7,
                    "Error on block %zu: [%zu, %zu) -> try fastest",
                    b,
                    begin,
                    end);
            *dst = dstOriginal;
            return ZS_Entropy_encodeFastest(
                    dst, src, srcSize, elementSize, params);
        }

        size_t const avail2 = ZL_WC_avail(dst);
        ZL_DLOG(V7, "block size = %zu", avail1 - avail2);
    }
    ZL_DLOG(V7, "SPLIT END");
    return ZL_returnSuccess();
}

static ZL_Report ZS_Entropy_encodeMulti(
        ZL_WC* dst,
        void const* src,
        size_t srcSize,
        size_t elementSize,
        ZS_Entropy_EncodeParameters const* params)
{
    ZS_Entropy_EncodeParameters paramsCopy = *params;
    paramsCopy.allowedTypes &= ~ZS_Entropy_TypeMask_multi;
    size_t const numBlocks = (srcSize + ZS_ENTROPY_BLOCK_SPLIT_FIXED_SIZE - 1)
            / ZS_ENTROPY_BLOCK_SPLIT_FIXED_SIZE;
    ZL_DLOG(V7, "MULTI %zu - %zu", srcSize, numBlocks);
    ZL_RET_R_IF_ERR(ZS_Multi_writeHeader(dst, numBlocks));
    // TODO: Add smarter block splitting here...
    while (srcSize > 0) {
        size_t const blockSize =
                ZL_MIN(srcSize, ZS_ENTROPY_BLOCK_SPLIT_FIXED_SIZE);
        size_t const avail1 = ZL_WC_avail(dst);
        ZL_DLOG(V7, "Encoding block...");
        ZL_RET_R_IF_ERR(ZS_Entropy_encode(
                dst, src, blockSize, elementSize, &paramsCopy));
        size_t const avail2 = ZL_WC_avail(dst);
        ZL_DLOG(V7, "block size = %zu", avail1 - avail2);
        src = (uint8_t const*)src
                + ZS_ENTROPY_BLOCK_SPLIT_FIXED_SIZE * elementSize;
        srcSize -= blockSize;
    }
    ZL_DLOG(V7, "MULTI END");
    return ZL_returnSuccess();
}

static ZL_Report ZS_Entropy_encodeHufImpl(
        ZL_WC* dst,
        void const* src,
        size_t srcSize,
        size_t elementSize,
        uint64_t maxSymbol,
        ZS_Entropy_EncodeParameters const* params)
{
    ZL_DLOG(V7, "HUF");
    ZL_WC dst2 = *dst;
    ZL_ASSERT_GE(srcSize, 2);
    ZL_ASSERT_EQ(elementSize, 1);
    size_t const headerSize = ZS_HufAndFse_headerSize(srcSize, srcSize);
    if (ZL_WC_avail(dst) < headerSize) {
        ZL_DLOG(V7, "dst too small");
        return ZS_Entropy_encodeFastest(dst, src, srcSize, elementSize, params);
    }
    uint8_t* const header = ZL_WC_ptr(dst);
    ZL_WC_advance(dst, headerSize);

    if (srcSize > ZS_HUF_MAX_BLOCK_SIZE) {
        ZL_DLOG(ERROR, "Multi must be supported for large sources...");
        ZL_RET_R_ERR(GENERIC);
    }

    uint32_t const maxTableLog = ZL_MIN(HUF_TABLELOG_MAX, params->maxTableLog);
    size_t hufCSize            = HUF_compress2(
            ZL_WC_ptr(dst), ZL_WC_avail(dst), src, srcSize, 255, maxTableLog);
    if (HUF_isError(hufCSize)) {
        hufCSize = 0;
    }
    size_t const maxDstSize = ZS_Entropy_entropySizeBound(
            srcSize, elementSize, (uint32_t)maxSymbol, params->allowedTypes);
    if (hufCSize >= maxDstSize || hufCSize == 0) {
        *dst = dst2;
        if (maxSymbol < 128
            && (params->allowedTypes & ZS_Entropy_TypeMask_bit)) {
            return ZS_Entropy_encodeBit(
                    dst, src, srcSize, elementSize, maxSymbol, params);
        }
        return ZS_Entropy_encodeFastest(dst, src, srcSize, elementSize, params);
    }
    ZL_WC_advance(dst, hufCSize);
    return ZS_HufAndFse_writeHeader(
            header,
            headerSize,
            srcSize,
            hufCSize,
            0 /* TODO: Support tableMode */,
            false,
            ZS_Entropy_Type_huf);
}

static ZL_Report ZS_Entropy_encodeFseImpl(
        ZL_WC* dst,
        void const* src,
        size_t srcSize,
        size_t elementSize,
        uint64_t maxSymbol,
        ZS_Entropy_EncodeParameters const* params)
{
    ZL_DLOG(V7, "FSE");
    ZL_WC dst2 = *dst;
    ZL_ASSERT_EQ(elementSize, 1);
    size_t const headerSize = ZS_HufAndFse_headerSize(srcSize, srcSize);
    if (ZL_WC_avail(dst) < headerSize) {
        ZL_RET_R_ERR(GENERIC);
    }
    uint8_t* const header = ZL_WC_ptr(dst);
    ZL_WC_advance(dst, headerSize);

    uint32_t const maxTableLog = ZL_MIN(FSE_MAX_TABLELOG, params->maxTableLog);
    size_t const fseCSize      = FSE_compress2(
            ZL_WC_ptr(dst),
            ZL_WC_avail(dst),
            src,
            srcSize,
            255,
            maxTableLog,
            params->fseNbStates);
    size_t const maxDstSize = ZS_Entropy_entropySizeBound(
            srcSize, elementSize, (uint32_t)maxSymbol, params->allowedTypes);
    if (fseCSize >= maxDstSize || fseCSize <= 1
        || fseCSize == (size_t)-(int)ZSTD_error_dstSize_tooSmall) {
        *dst = dst2;
        if (maxSymbol < 128
            && (params->allowedTypes & ZS_Entropy_TypeMask_bit)) {
            return ZS_Entropy_encodeBit(
                    dst, src, srcSize, elementSize, maxSymbol, params);
        }
        return ZS_Entropy_encodeFastest(dst, src, srcSize, elementSize, params);
    }
    if (FSE_isError(fseCSize)) {
        ZL_RET_R_ERR(GENERIC);
    }
    ZL_WC_advance(dst, fseCSize);
    return ZS_HufAndFse_writeHeader(
            header,
            headerSize,
            srcSize,
            fseCSize,
            0 /* TODO: Support tableMode */,
            0 /* format */,
            ZS_Entropy_Type_fse);
}

static ZL_Report ZS_Bit_writeHeader(ZL_WC* dst, size_t srcSize, size_t numBits)
{
    ZL_ASSERT_LT(numBits, 32);
    int const hdr = (int)ZS_Entropy_Type_bit /* type: bits [0, 3) */
            | ((int)numBits << 3) /* num bits: bits [3, 8) */;
    size_t const varintSize = ZL_varintSize(srcSize);
    size_t const hdrSize    = 1 + varintSize;
    if (ZL_WC_avail(dst) < hdrSize) {
        ZL_RET_R_ERR(GENERIC);
    }
    ZL_WC_push(dst, (uint8_t)hdr);
    ZL_WC_pushVarint(dst, srcSize);
    ZL_DLOG(V7,
            "Decoded size = %llu (headerSize = %u)",
            (unsigned long long)srcSize,
            (unsigned)hdrSize);
    return ZL_returnSuccess();
}

static ZL_Report ZS_Entropy_encodeBit(
        ZL_WC* dst,
        void const* src,
        size_t srcSize,
        size_t elementSize,
        uint64_t maxSymbolValue,
        ZS_Entropy_EncodeParameters const* params)
{
    ZL_DLOG(V7, "Bit encoding...");
    ZL_WC dst2 = *dst;
    if (elementSize > 2) {
        ZL_RET_R_ERR(GENERIC);
    }
    size_t const numBits = 1
            + (size_t)(maxSymbolValue == 0
                               ? 0
                               : ZL_highbit32((unsigned)(maxSymbolValue)));
    ZL_ASSERT_LE(numBits, 8 * elementSize);
    if (numBits == 8 * elementSize) {
        ZL_DLOG(V7, "choosing fastest (no bits saved)");
        return ZS_Entropy_encodeFastest(dst, src, srcSize, elementSize, params);
    }
    ZL_Report const hdr  = ZS_Bit_writeHeader(dst, srcSize, numBits);
    size_t const dstSize = (srcSize * numBits + 7) / 8;
    if (ZL_isError(hdr) || ZL_WC_avail(dst) < dstSize
        || dstSize >= (srcSize * elementSize - 1)) {
        *dst = dst2;
        ZL_DLOG(V7, "source is too small to get gains (or dst size too small)");
        return ZS_Entropy_encodeFastest(dst, src, srcSize, elementSize, params);
    }

    ZS_bitpackEncode(
            ZL_WC_ptr(dst), dstSize, src, srcSize, elementSize, (int)numBits);

    ZL_WC_advance(dst, dstSize);
    return ZL_returnSuccess();
}

ZL_Report ZS_Entropy_encode(
        ZL_WC* dst,
        void const* src,
        size_t srcSize,
        size_t elementSize,
        ZS_Entropy_EncodeParameters const* params)
{
    ZL_DLOG(V7,
            "ZS_Entropy_encode(ZL_WC_avail(dst) = %zu, srcSize = %zu, elementSize = %zu)",
            ZL_WC_avail(dst),
            srcSize,
            elementSize);

    if ((params->allowedTypes & ZS_Entropy_TypeMask_multi)
        && params->blockSplits) {
        return ZS_Entropy_encodeBlockSplit(
                dst, src, srcSize, elementSize, params);
    }

    if (ZS_Entropy_useFastest(srcSize, elementSize, params)) {
        return ZS_Entropy_encodeFastest(dst, src, srcSize, elementSize, params);
    }

    // Use the MULTI encoding function for large blocks or for slower
    // compressions.
    // It will decide on block splitting strategy.
    if ((params->allowedTypes & ZS_Entropy_TypeMask_multi)
        && (srcSize > ZS_ENTROPY_MULTI_THRESHOLD
            || params->encodeSpeed.baseline <= ZL_EncodeSpeedBaseline_slower)) {
        ZL_WC dst2 = *dst;
        ZL_Report const ret =
                ZS_Entropy_encodeMulti(dst, src, srcSize, elementSize, params);
        if (ZL_isError(ret)) {
            *dst = dst2;
            return ZS_Entropy_encodeFastest(
                    dst, src, srcSize, elementSize, params);
        }
        return ret;
    }

    // 2 byte sources are handled by LA Huffman
    if (elementSize == 2) {
        return ZS_Entropy_encodeLAHuf(dst, src, srcSize, elementSize, params);
    }

    ZL_ASSERT_EQ(elementSize, 1);

    ZL_Histogram const* histogram = NULL;
    ZL_Histogram8 histStorage;
    if (!params->precomputedHistogram) {
        // TODO: Build histogram
        ZL_Histogram_init(&histStorage.base, 255);
        ZL_Histogram_build(&histStorage.base, src, srcSize, 1);
        histogram = &histStorage.base;
    }

    ZS_Entropy_Type_e const type = ZS_Entropy_selectType(histogram, params);
    if (type == ZS_Entropy_Type_huf) {
        return ZS_Entropy_encodeHufImpl(
                dst, src, srcSize, elementSize, histogram->maxSymbol, params);
    }
    if (type == ZS_Entropy_Type_fse) {
        return ZS_Entropy_encodeFseImpl(
                dst, src, srcSize, elementSize, histogram->maxSymbol, params);
    }
    if (type == ZS_Entropy_Type_bit) {
        return ZS_Entropy_encodeBit(
                dst, src, srcSize, elementSize, histogram->maxSymbol, params);
    }
    return ZS_Entropy_encodeFastest(dst, src, srcSize, elementSize, params);
}

ZL_Report ZS_Constant_encode(ZL_WC* dst, void const* src, size_t elementSize)
{
    ZL_DLOG(V7, "Constant");
    if (ZL_WC_avail(dst) < elementSize) {
        ZL_RET_R_ERR(GENERIC);
    }
    switch (elementSize) {
        case 1:
            ZL_WC_push(dst, *(uint8_t const*)src);
            break;
        case 2:
            ZL_WC_pushCE16(dst, ZL_read16(src));
            break;
        case 4:
            ZL_WC_pushCE32(dst, ZL_read32(src));
            break;
        case 8:
            ZL_WC_pushCE64(dst, ZL_read64(src));
            break;
        default:
            ZL_RET_R_ERR(GENERIC);
    }
    return ZL_returnSuccess();
}

ZL_Report
ZS_Raw_encode(ZL_WC* dst, void const* src, size_t srcSize, size_t elementSize)
{
    ZL_DLOG(V7, "RAW");
    if (srcSize == 0) {
        return ZL_returnSuccess();
    }
    size_t const dstSize = srcSize * elementSize;
    ZL_DLOG(V7, "avail = %zu | dstSize = %zu", ZL_WC_avail(dst), dstSize);
    if (ZL_WC_avail(dst) < dstSize) {
        ZL_RET_R_ERR(GENERIC);
    }
    ZL_RC srcRC    = ZL_RC_wrap(src, srcSize * elementSize);
    size_t const a = ZL_WC_avail(dst);
    ZS_Endianness_transform(
            dst,
            &srcRC,
            ZL_Endianness_canonical,
            ZL_Endianness_host(),
            elementSize);
    ZL_ASSERT_EQ(ZL_RC_avail(&srcRC), 0);
    size_t const b = ZL_WC_avail(dst);
    ZL_DLOG(V7, "transformed = %zu (srcSize = %zu)", a - b, srcSize);
    return ZL_returnSuccess();
}
